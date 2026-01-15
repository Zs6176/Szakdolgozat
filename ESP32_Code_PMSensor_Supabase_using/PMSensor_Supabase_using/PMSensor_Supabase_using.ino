#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "Adafruit_LTR390.h"
#include <SoftwareSerial.h>
#include <esp_sds011.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "time.h"
#include "secrets.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h> // Mutexhez

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

// Időkezelés
time_t last_valid_time = 0; 
bool time_synced = false; 

// Mutex a fájlrendszer védelmére
SemaphoreHandle_t fileMutex;

// Szenzor objektumok
bool is_SDS_running = true;
Adafruit_LTR390 ltr = Adafruit_LTR390();
Adafruit_BME280 bme;

// SDS011
HardwareSerial& serialSDS(Serial2);
Sds011Async< HardwareSerial > sds011(serialSDS);
constexpr int pm_tablesize = 20;
int pm25_table[pm_tablesize];
int pm10_table[pm_tablesize];
volatile bool sds011_data_ready = false;

// NTP beállítások (CEST: GMT+1, +1 óra nyári időszámítás)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;     // UTC + 1 óra
const int   daylightOffset_sec = 3600; // +1 óra nyári időszámítás

// --- Elődeklarációk ---
void uploadOfflineDataTask(void * parameter);
bool updateTimeFromNTP();
void saveOfflineDataSafe(String jsonData);

// --- Segédfüggvények az időhöz ---
time_t parseIsoTime(String isoTime) {
  struct tm tm = {0};
  strptime(isoTime.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
  return mktime(&tm);
}

String formatIsoTime(time_t timestamp) {
  struct tm *timeinfo;
  timeinfo = localtime(&timestamp);
  char buffer[30];
  strftime(buffer, 30, "%Y-%m-%dT%H:%M:%S", timeinfo);
  return String(buffer);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  // Mutex létrehozása
  fileMutex = xSemaphoreCreateMutex();

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

  // Kezdeti idő szinkronizálás NTP-ről
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (updateTimeFromNTP()) {
      Serial.println("Kezdeti idő NTP-ről beállítva (CEST).");
  }
setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); 
tzset();
  while (!ltr.begin()) { Serial.println("Couldn't find LTR390 sensor!"); delay(10); }
  while (!bme.begin(0x76)) { Serial.println("bme280 nem található!"); delay(10); }

  ltr.setResolution(LTR390_RESOLUTION_18BIT);
  pinMode(HEAT_PAD_PIN, OUTPUT);
  serialSDS.begin(9600, SERIAL_8N1, SDS_PIN_RX, SDS_PIN_TX);
  delay(100);

  loginToSupabase();

  // --- HÁTTÉRSZÁL INDÍTÁSA ---
  // Core 0-n futtatjuk (a loop a Core 1-en fut alapból), 16KB stack mérettel
  xTaskCreatePinnedToCore(
    uploadOfflineDataTask, /* Függvény neve */
    "OfflineUpload",       /* Task neve */
    16384,                  /* Stack méret (bájt) */
    NULL,                  /* Paraméterek */
    1,                     /* Prioritás (alacsony) */
    NULL,                  /* Handle */
    0                      /* Core ID (0 vagy 1) */
  );
}

void loop() {
  float temp, hum, temp_raw, hum_raw, pres, lux, wpm25, wpm10, uv;
  uint16_t als;
  uint8_t uv_index;
  float wfac = 1.0;
  int gain = 3;

  ensureWiFiConnected();
  if (shouldRefreshToken()) refreshSupabaseToken();
  checkAndRecoverI2C();
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
    // Serial.println(static_cast<int32_t>(deadline - millis()) / 1000); // Opcionális log
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

  temp_raw = bme.readTemperature();
  hum_raw = bme.readHumidity();
  pres = bme.readPressure() / 100.0F;

if (isSummerCorrectionActive()) {
    temp = 1.027622 * temp_raw - 0.00028865 * lux - 0.37998 * uv_index - 0.301226;
    hum  = 1.015463 * hum_raw + 0.000640061 * lux - 0.568064 * uv_index - 0.708233;
    Serial.println("Nyári korrekció AKTÍV");
} else {
    temp = calculateCorrectedTemp(lux,temp_raw);
    hum  = calculateCorrectedHum(lux,hum_raw,temp_raw);
    Serial.println("Nyári korrekció INAKTÍV");
}

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
      if (hum >= 70 && remaining < 30000) {
        Serial.println("Heat pad ÁLLAPOT: BEKAPCSOLVA");
        digitalWrite(HEAT_PAD_PIN, HIGH);
      } else if (hum >= 60 && remaining < 20000) {
        Serial.println("Heat pad ÁLLAPOT: BEKAPCSOLVA");
        digitalWrite(HEAT_PAD_PIN, HIGH);
      }
    }

  }
  sds011.perform_work();
}
bool isSummerCorrectionActive() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // Ha nincs NTP, akkor NE legyen korrekció
    return false;
  }

  int month = timeinfo.tm_mon + 1;  // 0–11 → 1–12

  // Nyár + 1 hónap előtte/utána: május 1 – szeptember 30.
  if (month >= 5 && month <= 9) return true;           // június, július, augusztus


  return false;
}
// Hőmérséklet számítása
// Képlet: =HA(C>20, -2.09 + 0.461*D - 0.00009*C, -2.21 + 1.07*D + 0.127*C)
float calculateCorrectedTemp(float light_lux, float raw_temp) {
  float corrected_temp;

  if (light_lux > 20) {
    // Világosban (> 20 lux)
    corrected_temp = -2.09 + (0.461 * raw_temp) - (0.00009 * light_lux);
  } else {
    // Sötétben (<= 20 lux)
    corrected_temp = -2.21 + (1.07 * raw_temp) + (0.127 * light_lux);
  }
  
  return corrected_temp;
}

// Páratartalom számítása
// Képlet: =HA(C>20, 18.48 + 0.948*B + 1.2*D + 0.00036*C, 7.04 + 1.1*B + 0.058*D - 0.383*C)
float calculateCorrectedHum(float light_lux, float raw_hum, float raw_temp) {
  float corrected_hum;

  if (light_lux > 20) {
    // Világosban (> 20 lux)
    corrected_hum = 18.48 + (0.948 * raw_hum) + (1.2 * raw_temp) + (0.00036 * light_lux);
  } else {
    // Sötétben (<= 20 lux)
    corrected_hum = 7.04 + (1.1 * raw_hum) + (0.058 * raw_temp) - (0.383 * light_lux);
  }

  // Opcionális: Határok kezelése (hogy ne legyen 100% felett vagy 0% alatt)
  if (corrected_hum > 100.0) corrected_hum = 100.0;
  if (corrected_hum < 0.0) corrected_hum = 0.0;

  return corrected_hum;
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
  }
}

void checkUART() {
  if (!serialSDS.available()) {
    Serial.println("UART adat nincs — újrainicializálás...");
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
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      access_token = doc["access_token"].as<String>();
      refresh_token = doc["refresh_token"].as<String>();
      tokenTTL = doc["expires_in"].as<int>() * 1000;
      tokenTimestamp = millis();
      Serial.println("Login OK");
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
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      access_token = doc["access_token"].as<String>();
      refresh_token = doc["refresh_token"].as<String>();
      tokenTTL = doc["expires_in"].as<int>() * 1000;
      tokenTimestamp = millis();
      Serial.println("Token refreshed");
    }
  } else {
    Serial.print("Refresh error: "); Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

bool checkAndRecoverI2C() {
  byte addressesToScan[] = {
    0x76, // bme
    0x53  // LTR390
  };
  bool recovery_needed = false;

  // 1. Ellenőrzés
  for (byte address : addressesToScan) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
      Serial.print("!!! I2C HIBA ÉSZLELVE: Eszköz nem válaszol: 0x");
      Serial.println(address, HEX);
      recovery_needed = true;
      break; // Elég egy hiba, a busz valószínűleg "beragadt"
    }
  }

  // 2. Helyreállítás (ha szükséges)
  if (recovery_needed) {
    Serial.println("I2C busz helyreállítási kísérlet (restart nélkül)...");

    // A: I2C periféria újraindítása az ESP32-n
    Wire.end();
    delay(100); // Rövid szünet
    Wire.begin(SDA_PIN, SCL_PIN);
    delay(100);

    // B: Minden I2C szenzor könyvtárának újraindítása
    // (mintha a setup() futna le újra)
    Serial.println("Szenzorok szoftveres újraindítása...");

    if (!bme.begin(0x76)) {Serial.println("bme280 újraindítása sikertelen!"); 
    }
    if (!ltr.begin()) { 
      Serial.println("LTR390 újraindítása sikertelen!"); 
    } else {
      // Fontos: az LTR390 egyedi beállításait újra alkalmazni kell!
      ltr.setResolution(LTR390_RESOLUTION_18BIT); 
    }

    Serial.println("I2C helyreállítás befejezve. Ez a mérési ciklus kimarad.");
    return false; // Jelezzük a loop()-nak, hogy hiba volt
  }

  return true; // Minden rendben
}
// --- Időlekérés NTP-ről (fallback) ---
bool updateTimeFromNTP() {
    struct tm timeinfo;
    // 2000 ms timeout az NTP lekérésre
    if(!getLocalTime(&timeinfo, 2000)){ 
        Serial.println("Nem sikerült az NTP időlekérés.");
        return false;
    }
    time(&last_valid_time);
    time_synced = true;
    Serial.println("Idő frissítve NTP-ről: " + formatIsoTime(last_valid_time));
    return true;
}

// --- Biztonságos mentés Mutex-szel ---
void saveOfflineDataSafe(String jsonData) {
  // Megpróbáljuk megszerezni a zárat végtelen várakozással (portMAX_DELAY)
  if (xSemaphoreTake(fileMutex, portMAX_DELAY) == pdTRUE) {
    File file = LittleFS.open("/offline_data.json", "a");
    if (file) {
      file.println(jsonData);
      file.close();
      Serial.println("Adat offline mentve.");
    } else {
      Serial.println("Nem sikerült megnyitni a fájlt írásra");
    }
    // Zár feloldása
    xSemaphoreGive(fileMutex);
  }
}

// --- HÁTTÉRSZÁL (Task) ---
void uploadOfflineDataTask(void * parameter) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && access_token != "") {
      
      bool hasDataToProcess = false;

      // 1. LÉPÉS: Megnézzük van-e adat, és ha igen, átnevezzük a fájlt, 
      // hogy a fő szál nyugodtan írhasson az új fájlba.
      if (xSemaphoreTake(fileMutex, portMAX_DELAY) == pdTRUE) {
        if (LittleFS.exists("/offline_data.json")) {
          // Ha már létezik processing fájl, akkor az előző körben hiba volt,
          // azt folytatjuk, nem nevezzük át az újat még.
          if (!LittleFS.exists("/processing.json")) {
             LittleFS.rename("/offline_data.json", "/processing.json");
          }
          hasDataToProcess = true;
        } else if (LittleFS.exists("/processing.json")) {
          hasDataToProcess = true;
        }
        xSemaphoreGive(fileMutex);
      }

      // 2. LÉPÉS: Feldolgozás (ha van processing fájl)
      if (hasDataToProcess) {
        File file = LittleFS.open("/processing.json", "r");
        if (file) {
          String tempFileName = "/processing_temp.json";
          File tempFile = LittleFS.open(tempFileName, "w"); // Ide írjuk, ami MÉG nem ment el
          
          bool allSuccess = true;

          while(file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            // Feltöltés logika (Ugyanaz a HTTP kérés, de lokális változókkal)
            HTTPClient http;
            http.begin(supabase_rest_url);
            http.setTimeout(10000); // Kisebb timeout a háttérszálnak
            http.setReuse(false);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("apikey", supabase_key);
            http.addHeader("Authorization", "Bearer " + access_token);
            http.addHeader("Prefer", "return=representation");
            
            int httpResponseCode = http.POST(line);
            http.end();

            if (httpResponseCode == 201) {
               Serial.println("[Task] Sikeres offline feltöltés.");
            } else {
               Serial.print("[Task] Feltöltés hiba: ");
               Serial.println(httpResponseCode);
               // Ha nem sikerült, elmentjük a temp fájlba, hogy megmaradjon
               if(tempFile) tempFile.println(line);
               allSuccess = false;
               // Opcionális: Ha hálózati hiba van, megszakíthatjuk a ciklust
               if (httpResponseCode <= 0) break; 
            }
            // Kis szünet a processzor kímélése érdekében
            vTaskDelay(100 / portTICK_PERIOD_MS);
          }
          
          file.close();
          if(tempFile) tempFile.close();

          // 3. LÉPÉS: Takarítás
          if (allSuccess) {
            LittleFS.remove("/processing.json");
            LittleFS.remove(tempFileName); // Üres volt
          } else {
            // Ha maradt adat, a temp fájl lesz az új processing fájl
            LittleFS.remove("/processing.json");
            LittleFS.rename(tempFileName, "/processing.json");
          }
        }
      }
    }
    
    // Várakozás a következő ellenőrzésig (pl. 10 másodperc)
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

// --- ADATKÜLDÉS ÉS MENTÉS ---
void uploadToSupabase(float pres, float hum, float hum_raw, float lux, float wpm25, float temp, float temp_raw, int uv, float wpm10, float uv_raw, float als) {
  HTTPClient http;
  
  DynamicJsonDocument doc(1024);
  
  // JSON adatok kitöltése...
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
  http.addHeader("Prefer", "return=representation"); 

  int httpResponseCode = http.POST(jsonData);

  // --- SIKERES KÜLDÉS ---
  if (httpResponseCode == 201) {
    String response = http.getString();
    Serial.println("Sikeres feltöltés.");

    DynamicJsonDocument responseDoc(2048);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error) {
      String serverTimeStr;
      if (responseDoc.is<JsonArray>()) {
         serverTimeStr = responseDoc[0]["Measure_time"].as<String>();
      } else {
         serverTimeStr = responseDoc["Measure_time"].as<String>();
      }

      if (serverTimeStr != "null" && serverTimeStr != "") {
        last_valid_time = parseIsoTime(serverTimeStr);
        time_synced = true;
        Serial.print("Idő szinkronizálva a szerverről: ");
        Serial.println(serverTimeStr);
      }
    }
  } 
  // --- SIKERTELEN KÜLDÉS ---
  else {
    Serial.print("Hiba a POST-ban: ");
    Serial.println(http.errorToString(httpResponseCode));

    bool is_fresh_ntp = false; // Segédváltozó: most kaptunk-e friss időt?

    // Ha nincs még időnk, megpróbáljuk NTP-ről
    if (!time_synced) {
      Serial.println("Nincs szinkronizált idő. Próbálkozás NTP-vel...");
      if (updateTimeFromNTP()) {
          is_fresh_ntp = true; // Sikerült, ez egy FRISS, AKTUÁLIS idő
      }
    }

    if (time_synced && last_valid_time > 0) {
      Serial.println("Hálózati hiba. Offline mentés folyamatban...");
      
      if (!is_fresh_ntp) {
          last_valid_time += 300;
          Serial.println("Régi idő láncolása (+5 perc).");
      } else {
          Serial.println("Friss NTP idő használata (nincs növelés).");
      }

      // Beírjuk az időt a JSON-ba
      doc["Measure_time"] = formatIsoTime(last_valid_time);
      
      String offlineJson;
      serializeJson(doc, offlineJson);

      // Mentés fájlba
      saveOfflineDataSafe(offlineJson);
      
      Serial.print("Offline adat elmentve ezzel az időbélyeggel: ");
      Serial.println(formatIsoTime(last_valid_time));
      
    } else {
      Serial.println("KRITIKUS: Nincs érvényes időszinkron (NTP is fail). Adat eldobva.");
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