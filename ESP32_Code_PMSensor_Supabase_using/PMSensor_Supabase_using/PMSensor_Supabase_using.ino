#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "Adafruit_LTR390.h"
#include <SoftwareSerial.h>
#include <esp_sds011.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h> // Fájlrendszer a mentéshez
#include "time.h"     // Időkezeléshez
#include "secrets.h"

#define HEAT_PAD_PIN 2
#define SDS_PIN_RX 18
#define SDS_PIN_TX 14
#define SDA_PIN 5
#define SCL_PIN 4
#define HTTTPWAITE 15000

// --- Globális változók ---
String access_token = "";
String refresh_token = "";
unsigned long tokenTimestamp = 0;
unsigned long tokenTTL = 0;

// Utolsó érvényes időbélyeg (Epoch formátumban)
time_t last_valid_time = 0; 
bool time_synced = false; // Jelzi, ha már legalább egyszer kaptunk időt a szervertől vagy NTP-től

// Szenzor objektumok
bool is_SDS_running = true;
Adafruit_LTR390 ltr = Adafruit_LTR390();
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

// SDS011
HardwareSerial& serialSDS(Serial2);
Sds011Async< HardwareSerial > sds011(serialSDS);
constexpr int pm_tablesize = 20;
int pm25_table[pm_tablesize];
int pm10_table[pm_tablesize];
volatile bool sds011_data_ready = false;

// NTP beállítások (csak kezdő inicializáláshoz, utána a DB időt használjuk)
const char* ntpServer = "pool.ntp.org";

// --- Segédfüggvények az időhöz ---

// ISO String konvertálása time_t típusra (A Supabase válaszának feldolgozásához)
time_t parseIsoTime(String isoTime) {
  struct tm tm = {0};
  // Supabase formátum pl: 2023-11-13T18:30:00.123456+00:00
  // Mi csak az első részt dolgozzuk fel: YYYY-MM-DDTHH:MM:SS
  strptime(isoTime.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
  return mktime(&tm); // Átalakítás epoch másodpercekké
}

// time_t konvertálása ISO Stringre (Az offline mentéshez)
String formatIsoTime(time_t timestamp) {
  struct tm *timeinfo;
  timeinfo = localtime(&timestamp); // Vagy gmtime, ha UTC-t használsz a DB-ben
  char buffer[30];
  strftime(buffer, 30, "%Y-%m-%dT%H:%M:%S", timeinfo);
  return String(buffer);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  // LittleFS inicializálás
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected with IP: " + WiFi.localIP().toString());

  // Kezdeti idő szinkronizálás NTP-ről (biztonsági tartalék, amíg nincs DB válasz)
  configTime(0, 0, ntpServer); // UTC idő
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    time(&last_valid_time);
    time_synced = true;
    Serial.println("Kezdeti idő NTP-ről beállítva.");
  }

  while (!ltr.begin()) { Serial.println("Couldn't find LTR390 sensor!"); delay(10); }
  while (!aht.begin()) { Serial.println("AHT20 nem található!"); delay(10); }
  while (!bmp.begin(0x77)) { Serial.println("BMP280 nem található!"); delay(10); }

  ltr.setResolution(LTR390_RESOLUTION_18BIT);
  pinMode(HEAT_PAD_PIN, OUTPUT);
  serialSDS.begin(9600, SERIAL_8N1, SDS_PIN_RX, SDS_PIN_TX);
  delay(100);

  loginToSupabase();
}

void loop() {
  float temp, hum, temp_raw, hum_raw, pres, lux, wpm25, wpm10, uv;
  uint16_t als;
  uint8_t uv_index;
  float wfac = 1.0;
  int gain = 3;

  ensureWiFiConnected();
  if (shouldRefreshToken()) refreshSupabaseToken();
  checkI2C();
  checkUART();
  start_SDS();

  uint32_t deadline;
  constexpr uint32_t duty_s = 30;

  sds011.on_query_data_auto_completed([&](int n) {
    int pm25;
    int pm10;
    if (sds011.filter_data(n, pm25_table, pm10_table, pm25, pm10) && !isnan(pm10) && !isnan(pm25)) {
      wpm25 = float(pm25) / 10;
      wpm10 = float(pm10) / 10;
    }
    Serial.println(F("End Handling SDS011 query data"));
    sds011_data_ready = true;
  });

  if (!sds011.query_data_auto_async(pm_tablesize, pm25_table, pm10_table)) {
    Serial.println(F("measurement capture start failed"));
  }

  deadline = millis() + duty_s * 1000;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    delay(1000);
    Serial.println(static_cast<int32_t>(deadline - millis()) / 1000);
    sds011.perform_work();
  }
  digitalWrite(HEAT_PAD_PIN, LOW);
  Serial.println("Heat pad ÁLLAPOT: KIKAPCSOLVA");

  // UV mérés
  ltr.setGain(LTR390_GAIN_3);
  ltr.setMode(LTR390_MODE_UVS);
  delay(100);
  uv = ltr.readUVS();
  uv_index = ( uv / 2300.0 * wfac ) * 4 * (18.0 / gain);

  // ALS mérés
  gain = 1;
  ltr.setGain(LTR390_GAIN_1);
  ltr.setMode(LTR390_MODE_ALS);
  delay(100);
  als = ltr.readALS();
  lux = (0.6 * als) / (gain * (100.0 / 100.0)) * wfac;

  // Szenzor olvasás
  sensors_event_t humidity, temperature;
  aht.getEvent(&humidity, &temperature);
  temp_raw = temperature.temperature;
  hum_raw = humidity.relative_humidity;
  pres = bmp.readPressure() / 100.0F;

  // Korrekciók
  temp = 1.027622 * temp_raw - 0.00028865 * lux - 0.37998 * uv_index - 0.301226;
  hum = 1.015463 * hum_raw + 0.000640061 * lux - 0.568064 * uv_index - 0.708233;

  // SDS várakozás
  uint32_t timeout = millis() + 30000;
  while (!sds011_data_ready && millis() < timeout) {
    delay(100);
    sds011.perform_work();
  }

  // --- ADATKÜLDÉS HÍVÁSA ---
  uploadToSupabase(pres, hum, hum_raw, lux, wpm25, temp, temp_raw, uv_index, wpm10, uv, als);

  // SDS leállítás és várakozás
  constexpr uint32_t down_s = 270;
  stop_SDS();
  deadline = millis() + down_s * 1000;
  
  while ((int32_t)(deadline - millis()) > 0) {
    delay(1000);
    int32_t remaining = deadline - millis();
    if (digitalRead(HEAT_PAD_PIN) == LOW) {
      aht.getEvent(&humidity, &temperature);
      if (humidity.relative_humidity >= 70 && remaining < 30000) {
        Serial.println("Heat pad ÁLLAPOT: BEKAPCSOLVA");
        digitalWrite(HEAT_PAD_PIN, HIGH);
      } else if (humidity.relative_humidity >= 60 && remaining < 20000) {
        Serial.println("Heat pad ÁLLAPOT: BEKAPCSOLVA");
        digitalWrite(HEAT_PAD_PIN, HIGH);
      }
    }
  }
  sds011.perform_work();
}

// --- WiFi ---
void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi kapcsolat elveszett, újracsatlakozás...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi újracsatlakozva!" : "\nWiFi hiba!");
  }
}
void checkUART() {
  if (!serialSDS.available()) {
    Serial.println("UART adat nincs — újrainicializálás...");

    // UART port újraindítása
    serialSDS.end();
    delay(100);
    serialSDS.begin(9600, SERIAL_8N1, SDS_PIN_RX, SDS_PIN_TX);
    delay(200);
    
    Serial.println("UART újrainicializálva");
  }

}
// --- Token ---
bool shouldRefreshToken() {
  unsigned long elapsed = millis() - tokenTimestamp;
  unsigned long remaining = (tokenTTL > elapsed) ? tokenTTL - elapsed : 0;
  return (remaining < 5 * 60 * 1000);
}

void loginToSupabase() {
  HTTPClient http;
  http.begin(supabase_auth_url);
  http.setTimeout(HTTTPWAITE);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabase_key);

  String body = "{\"email\":\"" + String(user_email) + "\",\"password\":\"" + String(user_password) + "\"}";
  int httpCode = http.POST(body);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Login response: " + payload);
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      access_token = doc["access_token"].as<String>();
      refresh_token = doc["refresh_token"].as<String>();
      tokenTTL = doc["expires_in"].as<int>() * 1000;
      tokenTimestamp = millis();
      Serial.println("Access token: " + access_token);
    }
  } else {
    Serial.print("Login error: "); Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void refreshSupabaseToken() {
  if (refresh_token == "") { loginToSupabase(); return; }
  HTTPClient http;
  String url = String(supabase_base_url) + "/auth/v1/token?grant_type=refresh_token";
  http.begin(url);
  http.setTimeout(HTTTPWAITE);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabase_key);

  String body = "{\"refresh_token\":\"" + refresh_token + "\"}";
  int httpCode = http.POST(body);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Refresh response: " + payload);
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      access_token = doc["access_token"].as<String>();
      refresh_token = doc["refresh_token"].as<String>();
      tokenTTL = doc["expires_in"].as<int>() * 1000;
      tokenTimestamp = millis();
      Serial.println("Új access token: " + access_token);
    }
  } else {
    Serial.print("Refresh error: "); Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void checkI2C() {
  Wire.beginTransmission(0x77);
  if (Wire.endTransmission() != 0) {
    Serial.println("I2C hiba, újraindítás...");
    ESP.restart();
  }
}

// --- Offline Mentés Függvény ---
void saveOfflineData(String jsonData) {
  File file = LittleFS.open("/offline_data.json", "a"); // Hozzáfűzés mód
  if (!file) {
    Serial.println("Nem sikerült megnyitni a fájlt írásra");
    return;
  }
  file.println(jsonData);
  file.close();
  Serial.println("Adat mentve az offline tárolóba.");
}

// --- Feltöltés ---
void uploadToSupabase(float pres, float hum, float hum_raw, float lux, float wpm25, float temp, float temp_raw, int uv, float wpm10, float uv_raw, float als) {
  HTTPClient http;
  
  // JSON létrehozása ArduinoJson-nal a biztonságos kezelésért
  DynamicJsonDocument doc(1024);
  
  doc["Atmospheric_pressure"] = pres;
  doc["Humidity"] = hum;
  doc["Humidity_raw"] = hum_raw;
  doc["Light_quantity"] = lux;
  doc["PM25"] = wpm25;
  doc["Temperature"] = temp;
  doc["Temperature_raw"] = temp_raw;
  doc["UV_raw"] = uv_raw;
  doc["ALS"] = als;
  doc["UV"] = uv;
  doc["PM10"] = wpm10;
  
  // Megjegyzés: Normál esetben NEM küldünk Measure_time-ot, a DB generálja.

  String jsonData;
  serializeJson(doc, jsonData);

  Serial.print("Küldés Supabase-be: ");
  Serial.println(jsonData);

  http.begin(supabase_rest_url);
  http.setTimeout(HTTTPWAITE);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabase_key);
  http.addHeader("Authorization", "Bearer " + access_token);
  
  // FONTOS: Ez kéri a Supabase-t, hogy küldje vissza a beszúrt adatot (benne az idővel)
  http.addHeader("Prefer", "return=representation"); 

  int httpResponseCode = http.POST(jsonData);

  // --- SIKERES KÜLDÉS (200 vagy 201) ---
  if (httpResponseCode >= 200 && httpResponseCode < 300) {
    String response = http.getString();
    Serial.print("Sikeres feltöltés. Válasz: ");
    Serial.println(response);

    // A válaszból kinyerjük a DB által generált Measure_time-ot
    DynamicJsonDocument responseDoc(2048);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error) {
      // Supabase tömböt ad vissza, ha return=representation van, [0] az első elem
      String serverTimeStr;
      if (responseDoc.is<JsonArray>()) {
         serverTimeStr = responseDoc[0]["Measure_time"].as<String>();
      } else {
         serverTimeStr = responseDoc["Measure_time"].as<String>();
      }

      if (serverTimeStr != "null" && serverTimeStr != "") {
        last_valid_time = parseIsoTime(serverTimeStr);
        time_synced = true;
        Serial.print("Szerver idő szinkronizálva: ");
        Serial.println(serverTimeStr);
      }
    }
  } 
  else{
    Serial.print("Hiba a POST-ban: ");
    Serial.println(http.errorToString(httpResponseCode));

    // Csak akkor mentünk, ha már van legalább egy érvényes időnk
    if (time_synced) {
      Serial.println("Hálózati hiba. Offline mentés folyamatban...");
      
      // 1. Hozzáadunk 5 percet (300 másodperc) az utolsó ismert időhöz
      last_valid_time += 300;

      // 2. Beillesztjük az új időt a JSON-ba
      doc["Measure_time"] = formatIsoTime(last_valid_time);
      
      // 3. Újrageneráljuk a stringet
      String offlineJson;
      serializeJson(doc, offlineJson);

      // 4. Mentés fájlba
      saveOfflineData(offlineJson);
      
      Serial.print("Offline adat mentve ezzel az idővel: ");
      Serial.println(formatIsoTime(last_valid_time));
    } else {
      Serial.println("Nincs érvényes időszinkron, nem tudok offline menteni.");
    }
  }
  http.end();
}

void start_SDS() {
  Serial.println(F("Start wakeup SDS011"));
  if (sds011.set_sleep(false)) { is_SDS_running = true; }
  Serial.println(F("End wakeup SDS011"));
}

void stop_SDS() {
  Serial.println(F("Start sleep SDS011"));
  if (sds011.set_sleep(true)) { is_SDS_running = false; }
  Serial.println(F("End sleep SDS011"));
}