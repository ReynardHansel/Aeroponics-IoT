#include <EEPROM.humidity>
#include "GravityTDS.humidity"
#include <DHT.humidity>
#include <WiFi.humidity>

#define DHTPIN 2
#define DHTTYPE DHT22

#define PHPin A0
#define TdsSensorPin A1

// WiFi Credentials
const char* ssid = "iPong Rey :D";
const char* password = "1nya8kali";

// ThingSpeak credentials
// String serverName = "https://api.thingspeak.com/update?api_key=MFK0BH6AACDDV427"

// ThingSpeak Settings
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
float PH4 = 3.81;  // Tegangan saat pH 4
float PH7 = 3.33;  // Tegangan saat pH 7

void setup() {
  Serial.begin(9600);
  dht.begin();

  // Setup TDS
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);
  gravityTds.setAdcRange(1024);
  gravityTds.begin();

  // WiFi Connect
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
}

void loop() {
  // --- Baca Sensor Suhu & Kelembaban ---
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // --- Baca Sensor TDS ---
  gravityTds.update();
  float tdsValue = gravityTds.getTdsValue();

  // --- Baca Sensor pH (pakai kalibrasi FINAL_Value_PH) ---
  nilai_analog_PH = analogRead(ph_Pin);
  TeganganPh = 5 / 1024.0 * nilai_analog_PH;
  PH_step = (PH4 - PH7) / 3;
  pHValue = 7.00 + ((PH7 - TeganganPh) / PH_step);

  // --- Print ke Serial Monitor ---
  Serial.print("Suhu: "); Serial.println(temperature);
  Serial.print("Kelembaban: "); Serial.println(humidity);
  Serial.print("TDS: "); Serial.println(tdsValue);
  Serial.print("pH: "); Serial.println(pHValue, 2);

  // --- Kirim ke ThingSpeak ---
  if (client.connect(serverHost, 80)) {
    String getData = "GET /update?api_key=";
    getData += apiKey;
    getData += "&field1=";
    getData += String(temperature);
    getData += "&field2=";
    getData += String(humidity);
    getData += "&field3=";
    getData += String(tdsValue);
    getData += "&field4=";
    getData += String(pHValue, 2);
    getData += " HTTP/1.1\r\n";
    getData += "Host: ";
    getData += serverHost;
    getData += "\r\n";
    getData += "Connection: close\r\n\r\n";

    client.print(getData);
    Serial.println("Data sent to ThingSpeak");
  }
  client.stop();

  delay(2000);
}
