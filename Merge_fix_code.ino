#include <EEPROM.h>
#include "GravityTDS.h"
#include <DHT.h>
#include <WiFiS3.h>   // UNO R4 WiFi library

// --- ⚙️ PIN & SENSOR CONFIG ---
#define DHTPIN 2
#define DHTTYPE DHT22

#define PHPin A0
#define TdsSensorPin A1
#define relayPin 12   

// --- 🔧 RELAY CONFIGURATION ---
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// --- 📶 CREDENTIALS ---
const char* ssid = "x";
const char* password = "y";

// ThingSpeak credentials
const char* serverHost = "api.thingspeak.com";
const char* apiKey = "z";

WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);
GravityTDS gravityTds;

// --- 🧪 SENSOR VARIABLES ---
const int ph_Pin = PHPin;
float pHValue = 0;
float PH_step;
int nilai_analog_PH;
double TeganganPh;
float PH4 = 3.69;
float PH7 = 3.33;

// --- 📡 CONNECTION CONTROL ---
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_INTERVAL_MS  = 30000;
unsigned long lastConnectAttempt = 0;

// --- 💦 PUMP & RELAY CONTROL ---
const unsigned long wateringInterval = 3600000;  // 1 hour
const unsigned long PUMP_RUN_DURATION = 60000;   // 60 seconds
const float TEMP_THRESHOLD = 35.0;               // 35°C

// 🆕 LIMITER / COOLDOWN
const unsigned long THERMAL_COOLDOWN = 1800000;  // 30 minutes

// State Variables
unsigned long lastWateredTime = 0;
unsigned long pumpStartTime = 0;
bool pumpRunning = false;

// --- ☁️ THINGSPEAK UPLOAD CONTROL ---
unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 900000;    // 10 min

// --- 📊 SENSOR READING STRUCT ---
struct SensorReading {
  float temperature;
  float humidity;
  float tds;
  float ph;
};

// --- 🛠 HELPER FUNCTIONS ---
const char* wifiStatusToString(int s) {
  switch (s) {
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
    default: return "UNKNOWN_STATUS";
  }
}

void flushResponse() {
  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 2000) break;
  }
  while (client.available()) {
    client.read(); 
  }
}

void sendPumpStatus(int state) {
  if (WiFi.status() == WL_CONNECTED) {
    if (client.connect(serverHost, 80)) {
      String getData = "GET /update?api_key=";
      getData += apiKey;
      getData += "&field5=" + String(state);
      getData += " HTTP/1.1\r\nHost: ";
      getData += serverHost;
      getData += "\r\nConnection: close\r\n\r\n";
      client.print(getData);
      flushResponse();
      Serial.print(">> Pump Status ("); Serial.print(state); Serial.println(") sent & Socket cleared.");
    } else {
      Serial.println(">> Connection failed while sending Pump Status.");
    }
    client.stop(); 
  }
}

void printPumpCountdown(unsigned long currentRuntime) {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 30000) { 
    unsigned long remainingSeconds = (PUMP_RUN_DURATION - currentRuntime) / 1000;
    Serial.print("   [Status] Pump is running... ");
    Serial.print(remainingSeconds);
    Serial.println("s remaining.");
    lastPrint = millis();
  }
}

// --- 📊 UNIFIED SENSOR READING FUNCTION ---
SensorReading readSensors() {
  SensorReading reading;
  reading.temperature = dht.readTemperature();
  reading.humidity = dht.readHumidity();
  gravityTds.setTemperature(reading.temperature);
  gravityTds.update();
  reading.tds = gravityTds.getTdsValue();
  int adc = analogRead(ph_Pin);
  float voltage = 5.0 / 1024.0 * adc;
  reading.ph = (7.00 + ((PH7 - voltage) / PH_step));
  return reading;
}

// --- 🧠 CORE PUMP LOGIC ---
void managePumpLoop() {
  unsigned long currentMillis = millis();
  if (!pumpRunning) {
    if (currentMillis - lastWateredTime < THERMAL_COOLDOWN) return; 

    float temperature = dht.readTemperature();
    if (isnan(temperature)) return;

    bool isTooHot       = (temperature > TEMP_THRESHOLD);
    bool isScheduleTime = (currentMillis - lastWateredTime >= wateringInterval);

    if (isTooHot || isScheduleTime) {
      Serial.println(">>> STARTING PUMP <<<");
      digitalWrite(relayPin, RELAY_ON); 
      pumpRunning = true;
      pumpStartTime = currentMillis;
      lastWateredTime = currentMillis; 
      sendPumpStatus(1);
    }
  } 
  else {
    unsigned long runtime = currentMillis - pumpStartTime;
    if (runtime >= PUMP_RUN_DURATION) {
      Serial.println(">>> STOPPING PUMP (Time Complete) <<<");
      digitalWrite(relayPin, RELAY_OFF);
      pumpRunning = false;
      sendPumpStatus(0);
      lastUploadTime = millis(); 
    } 
    else {
      printPumpCountdown(runtime);
    }
  }
}

void attemptConnectWiFi() {
  Serial.print("Connecting to WiFi '"); Serial.print(ssid); Serial.print("' ");
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect. Will retry later.");
    lastConnectAttempt = millis();
  }
}

void setup() {
  Serial.begin(9600);
  while (!Serial) { ; }
  Serial.println("\nSystem Startup...");
  
  dht.begin();
  PH_step = (PH4 - PH7) / 3.0;
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);
  gravityTds.setAdcRange(1024);
  gravityTds.begin();

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, RELAY_OFF); 

  attemptConnectWiFi();
  delay(1000); 

  Serial.println("Startup: Executing initial pump cycle...");
  digitalWrite(relayPin, RELAY_ON);       
  pumpRunning = true;
  pumpStartTime = millis();
  lastWateredTime = millis();             
  Serial.println("Pump started (startup cycle)");
  sendPumpStatus(1);
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastConnectAttempt >= WIFI_RETRY_INTERVAL_MS) {
      attemptConnectWiFi();
    }
  }

  managePumpLoop();

  // --- INSTANT READINGS (Serial Monitor Only) ---
  SensorReading instantReading = readSensors();
  Serial.print("[Instant] T:"); Serial.print(instantReading.temperature);
  Serial.print(" | H:"); Serial.print(instantReading.humidity);
  Serial.print(" | TDS:"); Serial.print(instantReading.tds);
  Serial.print(" | pH:"); Serial.println(instantReading.ph);

  // --- 📡 UPLOAD LOGIC (BURST SAMPLING) ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastUploadTime >= uploadInterval && !pumpRunning) { 
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n--- STARTING BURST SAMPLING FOR UPLOAD ---");
      
      float totalTemp = 0;
      float totalHum = 0;
      float totalPh = 0;
      float totalTds = 0;
      int samples = 5;

      // ⏱️ BURST LOOP: Gather 5 samples, 2 seconds apart
      for (int i = 0; i < samples; i++) {
        Serial.print("Gathering sample "); Serial.print(i + 1); Serial.print("/"); Serial.println(samples);
        
        // 1. Wait 2 seconds (but keep updating TDS sensor during wait)
        unsigned long startWait = millis();
        while(millis() - startWait < 2000) {
          gravityTds.update(); 
        }

        // 2. Read Sensors using unified function
        SensorReading reading = readSensors();

        // 3. Print sample values
        Serial.print("  T:"); Serial.print(reading.temperature);
        Serial.print(" | H:"); Serial.print(reading.humidity);
        Serial.print(" | TDS:"); Serial.print(reading.tds);
        Serial.print(" | pH:"); Serial.println(reading.ph);
        
        // 4. Accumulate readings
        totalTemp += reading.temperature;
        totalHum += reading.humidity;
        totalTds += reading.tds;
        totalPh += reading.ph;
      }

      // ➗ CALCULATE AVERAGES
      float avgTemp = totalTemp / samples;
      float avgHum  = totalHum / samples;
      float avgTds  = totalTds / samples;
      float avgPh   = totalPh / samples;

      Serial.println(">> Averages Calculated:");
      Serial.print("   Avg Temp: "); Serial.println(avgTemp);
      Serial.print("   Avg TDS: "); Serial.println(avgTds);
      Serial.print("   Avg pH: "); Serial.println(avgPh);

      // 📤 UPLOAD
      Serial.println("Attempting ThingSpeak SENSOR upload...");
      if (client.connect(serverHost, 80)) {
        
        String getData = "GET /update?api_key=";
        getData += apiKey;
        getData += "&field1=" + String(avgTemp);
        getData += "&field2=" + String(avgHum);
        getData += "&field3=" + String(avgTds);
        getData += "&field4=" + String(avgPh, 2);
        getData += " HTTP/1.1\r\nHost: ";
        getData += serverHost;
        getData += "\r\nConnection: close\r\n\r\n";

        client.print(getData);
        flushResponse();
        Serial.println("Sensor Data sent & Socket cleared!");
      }
      client.stop();
      
      lastUploadTime = millis();  
    }
  }
  
  // Short delay for the loop
  delay(1000);
}
