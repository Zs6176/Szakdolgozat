#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_ST7789.h>
#include <SoftwareSerial.h>
#include <esp_sds011.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

#define UV_SENSOR_PIN 6
#define HEAT_PAD_PIN 2

#define SDS_PIN_RX 18
#define SDS_PIN_TX 14

#define SDA_PIN 5
#define SCL_PIN 4
/*
#define TFT_MOSI 35
#define TFT_CLK 36
#define TFT_DC 33
#define TFT_CS 34
#define TFT_RST 37
<<<<<<< HEAD:ESP32_Code_PMSensor_Supabase_using/PMSensor_Supabase_using/PMSensor_Supabase_using.ino
*/
/*
#define WIFI_SSID "YOUR_WIFI"
#define WIFI_PASSWORD "YOUR_PASSWORD"

const char* supabase_base_url = "https://YOUR_PROJECT.supabase.co";
const char* supabase_auth_url = "https://YOUR_PROJECT.supabase.co/auth/v1/token?grant_type=password";
const char* supabase_rest_url = "https://YOUR_PROJECT.supabase.co/rest/v1/DataBase";
const char* supabase_key = "YOUR_anonim_KEY";

const char* user_email = "YOUR_EMAIL";
const char* user_password = "YOUR_PASSWORD";
*/
=======

>>>>>>> 6ab3017d97d1744d4da0c2038313a1af08fa1d2a:ESP32_Code_PMSensor_Supabase_using/PMSensor_Supabase_using.ino

String access_token = "";
String refresh_token = "";
unsigned long tokenTimestamp = 0;
unsigned long tokenTTL = 0;  // ms-ben

//sds011 sensore actual status
bool is_SDS_running = true;

// Light Sensors create
Adafruit_VEML7700 veml;
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
//Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);

//SDS011
HardwareSerial& serialSDS(Serial2);
Sds011Async< HardwareSerial > sds011(serialSDS);
constexpr int pm_tablesize = 20;
int pm25_table[pm_tablesize];
int pm10_table[pm_tablesize];

volatile bool sds011_data_ready = false;
// --- Setup ---
void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected with IP: " + WiFi.localIP().toString());

  while (!veml.begin()) Serial.println("VEML7700 nem található!");
  while (!aht.begin()) Serial.println("AHT20 nem található!");
  while (!bmp.begin(0x77)) Serial.println("BMP280 nem található!");
/*
  tft.init(172, 320);
  tft.fillScreen(ST77XX_BLACK);
 // analogSetPinAttenuation(UV_SENSOR_PIN, ADC_0db); // 0 dB -> kb. 0..1.1V
 */
  pinMode(HEAT_PAD_PIN, OUTPUT);

  serialSDS.begin(9600, SERIAL_8N1, SDS_PIN_RX, SDS_PIN_TX);
  delay(100);

  loginToSupabase();
  
}

void loop() {
  float temp, hum, temp_raw, hum_raw, pres, lux, wpm25, wpm10;
  uint8_t uv;
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

  //read the sensor data
  sensors_event_t humidity, temperature;

  aht.getEvent(&humidity, &temperature);
  temp_raw = temperature.temperature;
  hum_raw = humidity.relative_humidity;
  pres = bmp.readPressure() / 100.0F;
  uv = UVSensor();
  lux = veml.readLux();
  temp = 1.027622 * temp_raw - 0.00028865 * lux - 0.37998 * uv - 0.301226;
  hum = 1.015463 * hum_raw + 0.000640061 * lux - 0.568064 * uv - 0.708233;

  // Wait until get the PM data
  uint32_t timeout = millis() + 30000;  // max 30 secount wait

  while (!sds011_data_ready && millis() < timeout) {
    delay(100);
    sds011.perform_work();
  }
/*
  if (sds011_data_ready) {
    WriteToLCD(
      temp,
      hum,
      pres,
      uv,
      lux,
      wpm25,
      wpm10);
  }
  */

  uploadToSupabase(pres, hum, hum_raw, lux, wpm25, temp, temp_raw, uv, wpm10,analogRead(UV_SENSOR_PIN));
  //Stop the sds sensor working
  constexpr uint32_t down_s = 270;

  stop_SDS();
  deadline = millis() + down_s * 1000;
  while ((int32_t)(deadline - millis()) > 0) {
    delay(1000);

    int32_t remaining = deadline - millis();

    if (digitalRead(HEAT_PAD_PIN) == LOW) {
      // Read humidity throughout the countdown
      aht.getEvent(&humidity, &temperature);
      if (humidity.relative_humidity >= 70 && remaining < 30000) {
        digitalWrite(HEAT_PAD_PIN, HIGH);
      } else if (humidity.relative_humidity >= 60 && remaining < 20000) {
        digitalWrite(HEAT_PAD_PIN, HIGH);
      }
    }
  }

  sds011.perform_work();
}

// --- WiFi újracsatlakozás ---
void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi kapcsolat elveszett, újracsatlakozás...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi újracsatlakozva!");
    } else {
      Serial.println("\nWiFi hiba, nem sikerült csatlakozni!");
    }
  }
}

// --- Token kezelés ---
bool shouldRefreshToken() {
  unsigned long elapsed = millis() - tokenTimestamp;
  unsigned long remaining = (tokenTTL > elapsed) ? tokenTTL - elapsed : 0;
  return (remaining < 5 * 60 * 1000);
}

void loginToSupabase() {
  HTTPClient http;
  http.begin(supabase_auth_url);
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
    Serial.println("Login error: " + String(httpCode));
  }
  http.end();
}

void refreshSupabaseToken() {
  if (refresh_token == "") {
    loginToSupabase();
    return;
  }

  HTTPClient http;
  String url = String(supabase_base_url) + "/auth/v1/token?grant_type=refresh_token";
  http.begin(url);
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
    Serial.println("Refresh error: " + String(httpCode));
  }
  http.end();
}

// --- I2C ellenőrzés ---
void checkI2C() {
  Wire.beginTransmission(0x77);  // BMP280 cím
  int error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("I2C hiba, újraindítás...");
    ESP.restart();
  }
}

// --- UART ellenőrzés ---
void checkUART() {
  if (!serialSDS.available()) {
    Serial.println("UART hiba (nincs adat a SDS011-től), újraindítás...");
    ESP.restart();
  }
}
void uploadToSupabase(float pres, float hum, float hum_raw, float lux, float wpm25, float temp, float temp_raw, int uv, float wpm10,int UVValue) {
  HTTPClient http;


  String jsonData = "{";
  jsonData += "\"Atmospheric_pressure\":" + String(pres) + ",";
  jsonData += "\"Humidity\":" + String(hum) + ",";
  jsonData += "\"Humidity_raw\":" + String(hum_raw) + ",";
  jsonData += "\"Light_quantity\":" + String(lux) + ",";
  jsonData += "\"PM25\":" + String(wpm25) + ",";
  jsonData += "\"Temperature\":" + String(temp) + ",";
  jsonData += "\"Temperature_raw\":" + String(temp_raw) + ",";
  jsonData += "\"UV\":" + String(uv) + ",";
  jsonData += "\"PM10\":" + String(wpm10)+ ",";
  jsonData += "\"UVValue\":" + String(UVValue);
  jsonData += "}";


  Serial.print("Küldés Supabase-be: ");
  Serial.println(jsonData);


  http.begin(supabase_rest_url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabase_key);
  http.addHeader("Authorization", "Bearer " + access_token);


  int httpResponseCode = http.POST(jsonData);


  if (httpResponseCode > 0) {
    Serial.print("HTTP válaszkód: ");
    Serial.println(httpResponseCode);
    Serial.println(http.getString());
  } else {
    Serial.print("Hiba a POST-ban: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

//Start the sds sensor ventilator
void start_SDS() {
  Serial.println(F("Start wakeup SDS011"));

  if (sds011.set_sleep(false)) { is_SDS_running = true; }

  Serial.println(F("End wakeup SDS011"));
}
//stop the sds sensor ventilator
void stop_SDS() {
  Serial.println(F("Start sleep SDS011"));

  if (sds011.set_sleep(true)) { is_SDS_running = false; }

  Serial.println(F("End sleep SDS011"));
}

int UVSensor() {
  float sensorVoltage; 
  float sensorValue;
  int uvindex;
  sensorValue = analogRead(UV_SENSOR_PIN);
  sensorVoltage = sensorValue/4095*0.9;

  if      (sensorVoltage < 0.05) uvindex=0;
  else if (sensorVoltage > 0.05 && sensorVoltage <= 0.227) uvindex = 1;
  else if (sensorVoltage > 0.227 && sensorVoltage <= 0.318) uvindex = 2;
  else if (sensorVoltage > 0.318 && sensorVoltage <= 0.408) uvindex = 3;
  else if (sensorVoltage > 0.408 && sensorVoltage <= 0.503) uvindex = 4;
  else if (sensorVoltage > 0.503 && sensorVoltage <= 0.606) uvindex = 5;
  else if (sensorVoltage > 0.606 && sensorVoltage <= 0.696) uvindex = 6;
  else if (sensorVoltage > 0.696 && sensorVoltage <= 0.795) uvindex = 7;
  else if (sensorVoltage > 0.795 && sensorVoltage <= 0.881) uvindex = 8;
  else if (sensorVoltage > 0.881 && sensorVoltage <= 0.976) uvindex = 9;
  else if (sensorVoltage > 0.976 && sensorVoltage <= 1.079) uvindex = 10;
  else if (sensorVoltage > 1.079 && sensorVoltage <= 1.170) uvindex = 11;
  else uvindex = 12;  
  
  return (uvindex);
}

/*
void WriteToLCD(float temp, float hum, float press, int uvindex, float lux, float pm25, float pm10) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(1);
  tft.setCursor(0, 15);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextWrap(true);
  tft.setTextSize(2);
  tft.print("Temperature: ");
  tft.print(temp);
  tft.print(" C\n");
  tft.print("Humidity: ");
  tft.print(hum);
  tft.print(" %\n");
  tft.print("Atm. pressure: ");
  tft.print(press);
  tft.print(" hPa\n");
  tft.print("uv index: ");
  tft.print(uvindex);
  tft.print("\n");
  tft.print("Fenysuruseg: ");
  tft.print(lux);
  tft.print("lx\n");
  tft.print("PM2.5: ");
  tft.print(pm25);
  tft.print(" ug/m3\n");
  tft.print("PM10: ");
  tft.print(pm10);
  tft.print(" ug/m3\n");
}
*/