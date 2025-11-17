// TODO: Add startup pump logic

#include <EEPROM.h>
#include "GravityTDS.h"
#include <DHT.h>
#include <WiFiS3.h>   // UNO R4 WiFi library

#define DHTPIN 2
#define DHTTYPE DHT22

#define PHPin A0
#define TdsSensorPin A1
#define relayPin 12   // Relay pin

// WiFi Credentials
const char* ssid = "SUMBER HEGAR 2";
const char* password = "42038620";

// ThingSpeak credentials
const char* serverHost = "api.thingspeak.com";
const char* apiKey = "MFK0BH6AACDDV427";

WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);
GravityTDS gravityTds;
// PH4502C_Sensor ph4502c(PHPin, A4);

// Variabel untuk PH
const int ph_Pin = PHPin;
float pHValue = 0;
float PH_step;
int nilai_analog_PH;
double TeganganPh;

// Kalibrasi PH4 & PH7
float PH4 = 3.81;
float PH7 = 3.33;


// --- Connection control ---
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_INTERVAL_MS  = 30000;
unsigned long lastConnectAttempt = 0;

// --- Relay control ---
unsigned long lastWateredTime = 0;
const unsigned long wateringInterval = 3600000; // 1 hour
unsigned long pumpStartTime = 0;
bool pumpRunning = false;
const unsigned long PUMP_RUN_DURATION = 300000; // 5 minutes

// --- ThingSpeak upload control ---
unsigned long lastUploadTime = 0;                 // ⬅️ New variable
const unsigned long uploadInterval = 3600000;     // ⬅️ 1 hour interval

// --- Helper: map WiFi.status() to readable string ---
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

void activateRelayPump() {
  float temperature = dht.readTemperature();
  unsigned long currentMillis = millis();

  if (!pumpRunning && (temperature > 29.0 || (currentMillis - lastWateredTime >= wateringInterval))) {
    digitalWrite(relayPin, HIGH);
    pumpRunning = true;
    pumpStartTime = currentMillis;
    lastWateredTime = currentMillis;
    Serial.println("Relay turned ON (pump started)");
  }

  if (pumpRunning) {
    if (currentMillis - pumpStartTime >= PUMP_RUN_DURATION) {
      digitalWrite(relayPin, LOW);
      pumpRunning = false;
      Serial.println("Relay turned OFF (pump run complete)");
    } else {
      unsigned long remaining = PUMP_RUN_DURATION - (currentMillis - pumpStartTime);
      static unsigned long lastRemainingPrint = 0;
      if (currentMillis - lastRemainingPrint >= 30000) {
        Serial.print("Pump running, ms remaining: ");
        Serial.println(remaining);
        lastRemainingPrint = currentMillis;
      }
    }
  }
}

void attemptConnectWiFi() {
  Serial.print("Connecting to WiFi '");
  Serial.print(ssid);
  Serial.print("' ");

  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  int st = WiFi.status();
  Serial.print("WiFi status: ");
  Serial.print(st);
  Serial.print(" - ");
  Serial.println(wifiStatusToString(st));

  if (st == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
    IPAddress ip = WiFi.localIP();
    Serial.print("Local IP: ");
    Serial.println(ip);
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("Failed to connect within timeout. Will retry later.");
    lastConnectAttempt = millis();
  }
}

void setup() {
  Serial.begin(9600);
  while (!Serial) { ; }
  Serial.println();
  Serial.println("Startup...");

  dht.begin();

  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);
  gravityTds.setAdcRange(1024);
  gravityTds.begin();

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  attemptConnectWiFi();

   // --- 🔹 Turn on pump once when device starts ---
  Serial.println("Startup detected: turning on pump...");
  digitalWrite(relayPin, HIGH);           // Turn relay ON
  pumpRunning = true;
  pumpStartTime = millis();
  lastWateredTime = millis();             // record watering time
  Serial.println("Pump started (startup cycle)");
}

void loop() {
  activateRelayPump();

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastConnectAttempt >= WIFI_RETRY_INTERVAL_MS) {
      attemptConnectWiFi();
    }
  }

  // --- Sensor readings ---
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  gravityTds.update();
  float tdsValue = gravityTds.getTdsValue();

  nilai_analog_PH = analogRead(ph_Pin);
  TeganganPh = 5.0 / 1024.0 * nilai_analog_PH;
  PH_step = (PH4 - PH7) / 3.0;
  pHValue = 7.00 + ((PH7 - TeganganPh) / PH_step);

  Serial.print("Suhu: "); Serial.println(temperature);
  Serial.print("Kelembaban: "); Serial.println(humidity);
  Serial.print("TDS: "); Serial.println(tdsValue);
  Serial.print("K value: "); Serial.println(gravityTds.getKvalue());
  Serial.print("pH: "); Serial.println(pHValue);

  // --- ThingSpeak upload (every 1 hour) ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastUploadTime >= uploadInterval) {  // ⬅️ Upload interval check
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Attempting ThingSpeak upload...");
      if (client.connect(serverHost, 80)) {
        String getData = "GET /update?api_key=";
        getData += apiKey;
        getData += "&field1=" + String(temperature);
        getData += "&field2=" + String(humidity);
        getData += "&field3=" + String(tdsValue);
        getData += "&field4=" + String(pHValue, 2);
        getData += " HTTP/1.1\r\nHost: ";
        getData += serverHost;
        getData += "\r\nConnection: close\r\n\r\n";

        client.print(getData);
        Serial.println("Data sent to ThingSpeak!");
      } else {
        Serial.println("Could not connect to ThingSpeak.");
      }
      client.stop();
      lastUploadTime = currentMillis;  // ⬅️ Reset timer after upload
    } else {
      Serial.println("Skipping ThingSpeak upload (WiFi not connected).");
    }
  }

  delay(2000);
}
