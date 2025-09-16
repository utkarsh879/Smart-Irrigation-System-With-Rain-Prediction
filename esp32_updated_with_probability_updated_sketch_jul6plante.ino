#define BLYNK_TEMPLATE_ID "TMPL3v0BJmR3D"
#define BLYNK_TEMPLATE_NAME "Plant Irrigation System"
#define BLYNK_AUTH_TOKEN "d3_hBZ4RAuQ5kXViBfqc2HVbPP1HSYl3"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// WiFi credentials
char ssid[] = "AKASH";
char pass[] = "akash56789";

// WeatherAPI
String weatherApiKey = "517bffccd60b4d13bd6152743251407";
String location = "22.497563,88.371241";
const String weatherURL = "http://api.weatherapi.com/v1/forecast.json?key=" + weatherApiKey + "&q=" + location + "&days=1&aqi=no&alerts=no";

// Pins
#define MOISTURE_PIN 34
#define PUMP_PIN 26
#define ALWAYS_HIGH_PIN 25

// Settings
int moistureThreshold = 40;
int rainThreshold = 80;
bool isAutoMode = true;
bool manualPumpState = false;
bool rainExpected = false;
bool useRainPrediction = true;

// Blynk virtual pins
#define VPIN_MOISTURE_RAW     V0
#define VPIN_MOISTURE_PERCENT V1
#define VPIN_PUMP_MANUAL      V2
#define VPIN_MODE_SWITCH      V3
#define VPIN_THRESHOLD_SLIDER V4
#define VPIN_MOISTURE_STATUS  V5
#define VPIN_TEMPERATURE      V6
#define VPIN_RAIN_PROBABILITY V7
#define VPIN_RAIN_SWITCH      V8

BlynkTimer timer;

void setup() {
  Serial.begin(115200);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(ALWAYS_HIGH_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(ALWAYS_HIGH_PIN, HIGH);

  // Step 1: Connect WiFi
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  delay(1000);

  // Step 2: Sync time (NTP)
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for NTP time...");
    delay(500);
  }
  Serial.println("Time synced!");

  // Step 3: Connect Blynk
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  // Timers
  timer.setInterval(60000L, fetchWeatherData);
  timer.setInterval(2000L, checkSoil);
}

int getMoisturePercent(int raw) {
  int percent = map(raw, 2800, 950, 0, 100);
  return constrain(percent, 0, 100);
}

String getMoistureStatus(int percent) {
  if (percent <= 35) return "Dry";
  else if (percent <= 70) return "Moist";
  else return "Wet";
}

void checkSoil() {
  int rawValue = analogRead(MOISTURE_PIN);
  int moisturePercent = getMoisturePercent(rawValue);
  String status = getMoistureStatus(moisturePercent);

  Serial.print("Moisture Raw: ");
  Serial.print(rawValue);
  Serial.print("  %: ");
  Serial.print(moisturePercent);
  Serial.print("  Status: ");
  Serial.println(status);

  Blynk.virtualWrite(VPIN_MOISTURE_RAW, rawValue);
  Blynk.virtualWrite(VPIN_MOISTURE_PERCENT, moisturePercent);
  Blynk.virtualWrite(VPIN_MOISTURE_STATUS, status);

  if (isAutoMode) {
    if (useRainPrediction && rainExpected) {
      Serial.println("[AUTO MODE] Rain expected. Pump OFF.");
      digitalWrite(PUMP_PIN, LOW);
    } else {
      if (moisturePercent < moistureThreshold) {
        Serial.println("[AUTO MODE] Soil dry. Pump ON.");
        digitalWrite(PUMP_PIN, HIGH);
      } else {
        Serial.println("[AUTO MODE] Soil OK. Pump OFF.");
        digitalWrite(PUMP_PIN, LOW);
      }
    }
  } else {
    digitalWrite(PUMP_PIN, manualPumpState ? HIGH : LOW);
    Serial.print("[MANUAL MODE] Pump is ");
    Serial.println(manualPumpState ? "ON" : "OFF");
  }
}

void fetchWeatherData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    Serial.println("Fetching hourly weather data...");
    http.begin(weatherURL);
    int httpCode = http.GET();
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(16384);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        float temperature = doc["current"]["temp_c"];
        Blynk.virtualWrite(VPIN_TEMPERATURE, temperature);
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");

        // Match current hour's forecast
        time_t now = time(nullptr);
        struct tm* now_tm = localtime(&now);

        int currentHour = now_tm->tm_hour;
        JsonArray hourly = doc["forecast"]["forecastday"][0]["hour"];
        int rainChance = 0;

        for (JsonObject hourData : hourly) {
          String timeStr = hourData["time"];
          int forecastHour = timeStr.substring(11, 13).toInt();

          if (forecastHour == currentHour) {
            rainChance = hourData["chance_of_rain"];
            break;
          }
        }

        Serial.print("Rain probability (this hour): ");
        Serial.print(rainChance);
        Serial.println(" %");

        Blynk.virtualWrite(VPIN_RAIN_PROBABILITY, rainChance);
        rainExpected = (rainChance >= rainThreshold);

        Serial.println(rainExpected ? "Rain likely - delaying pump." : "No rain expected.");
      } else {
        Serial.println("JSON parse error.");
        rainExpected = false;
      }
    } else {
      Serial.println("HTTP Request failed.");
      rainExpected = false;
    }

    http.end();
  } else {
    Serial.println("WiFi disconnected.");
    rainExpected = false;
  }
}

// Blynk handlers
BLYNK_WRITE(VPIN_PUMP_MANUAL) {
  manualPumpState = param.asInt();
}

BLYNK_WRITE(VPIN_MODE_SWITCH) {
  isAutoMode = param.asInt();
  Serial.print("Mode switched to: ");
  Serial.println(isAutoMode ? "AUTO" : "MANUAL");
}

BLYNK_WRITE(VPIN_THRESHOLD_SLIDER) {
  moistureThreshold = param.asInt();
  Serial.print("Moisture threshold set to: ");
  Serial.println(moistureThreshold);
}

BLYNK_WRITE(VPIN_RAIN_SWITCH) {
  useRainPrediction = param.asInt();
  Serial.print("Rain prediction mode: ");
  Serial.println(useRainPrediction ? "ENABLED" : "DISABLED");
}

void loop() {
  Blynk.run();
  timer.run();
}
