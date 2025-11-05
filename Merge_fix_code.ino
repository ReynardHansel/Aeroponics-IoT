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
const char* ssid = "Sumber hegar 23";
const char* password = "5nya8kali";

// ThingSpeak credentials
const char* serverHost = "api.thingspeak.com";
const char* apiKey = "MFK0BH6AACDDV427";

WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);
GravityTDS gravityTds;

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
const unsigned long wateringInterval = 3600000; // 1 hour in milliseconds
// Pump run timer
unsigned long pumpStartTime = 0;
bool pumpRunning = false;
const unsigned long PUMP_RUN_DURATION = 300000; // 5 minutes in milliseconds
// To change how long the pump runs for after being triggered, modify PUMP_RUN_DURATION.
// Example: for 2 minutes use 120000, for 10 minutes use 600000.

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

  // Start pump if temperature threshold or watering interval reached
  if (!pumpRunning && (temperature > 29.0 || (currentMillis - lastWateredTime >= wateringInterval))) {
    digitalWrite(relayPin, HIGH); // Turn relay ON
    pumpRunning = true;
    pumpStartTime = currentMillis;
    lastWateredTime = currentMillis; // record when we started watering
    Serial.println("Relay turned ON (pump started)");
  }

  // If pump is running, check if we've reached the run duration
  if (pumpRunning) {
    if (currentMillis - pumpStartTime >= PUMP_RUN_DURATION) {
      digitalWrite(relayPin, LOW); // Turn relay OFF after duration
      pumpRunning = false;
      Serial.println("Relay turned OFF (pump run complete)");
    } else {
      // Still running; optionally print remaining time occasionally
      unsigned long remaining = PUMP_RUN_DURATION - (currentMillis - pumpStartTime);
      // Print remaining time every 30 seconds to avoid spamming
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

  // Setup TDS
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);
  gravityTds.setAdcRange(1024);
  gravityTds.begin();

  // Setup relay
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // ensure relay off at start

  // Connect WiFi
  attemptConnectWiFi();
}

void loop() {
  activateRelayPump();

  // If not connected, retry WiFi periodically
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastConnectAttempt >= WIFI_RETRY_INTERVAL_MS) {
      attemptConnectWiFi();
    }
  }

  // Read sensors
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  gravityTds.update();
  float tdsValue = gravityTds.getTdsValue();

  nilai_analog_PH = analogRead(ph_Pin);
  TeganganPh = 5.0 / 1024.0 * nilai_analog_PH;
  PH_step = (PH4 - PH7) / 3.0;
  pHValue = 7.00 + ((PH7 - TeganganPh) / PH_step);

  // Print sensor data
  Serial.print("Suhu: "); Serial.println(temperature);
  Serial.print("Kelembaban: "); Serial.println(humidity);
  Serial.print("TDS: "); Serial.println(tdsValue);
  Serial.print("pH: "); Serial.println(pHValue, 2);

  // Print WiFi status
  int currentStatus = WiFi.status();
  Serial.print("WiFi status (now): ");
  Serial.print(currentStatus);
  Serial.print(" - ");
  Serial.println(wifiStatusToString(currentStatus));

  // Send to ThingSpeak if connected
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
      Serial.println("Data sent to ThingSpeak");
    } else {
      Serial.println("Could not connect to ThingSpeak.");
    }
    client.stop();
  } else {
    Serial.println("Skipping ThingSpeak upload (WiFi not connected).");
  }

  delay(2000);
}