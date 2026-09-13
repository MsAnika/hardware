#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WiFi credentials (update to your local network or test SSID)
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* api_base_url = "http://10.11.160.179:8000";

// Public MQTT Broker
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Initialize I2C LCD (Address 0x27, 16 columns, 2 rows)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Hardware Pin Definitions
const int PLUG_BUTTON_PIN = 14;  
const int VOLTAGE_POT_PIN = 34;  
const int CURRENT_POT_PIN = 35;  

bool isCharging = false;
unsigned long previousMillis = 0;
unsigned long lastMqttAttempt = 0;
float totalKwh = 0.0;
float totalBill = 0.0;

void postToApi(const char* path, const char* payload) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  String url = String(api_base_url) + path;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}

void setup_wifi() {
  delay(10);
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    lcd.print(".");
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected!");
  delay(1000);
}

void reconnect() {
  if (millis() - lastMqttAttempt < 5000) {
    return;
  }

  lastMqttAttempt = millis();
  if (client.connect("GridMitraESP32Client")) {
    client.subscribe("gridmitra/session/command");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming remote commands if needed
}

void setup() {
  pinMode(PLUG_BUTTON_PIN, INPUT_PULLUP);
  
  lcd.init();
  lcd.backlight();
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  lcd.setCursor(0, 0);
  lcd.print("GridMitra Ready");
  lcd.setCursor(0, 1);
  lcd.print("Plug in EV Cable");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  } else {
    client.loop();
  }

  bool buttonState = (digitalRead(PLUG_BUTTON_PIN) == LOW);

  if (buttonState && !isCharging) {
    isCharging = true;
    totalKwh = 0.0;
    totalBill = 0.0;
    postToApi("/api/hardware/button", "{\"pressed\":true,\"status\":\"CONNECTED\"}");
    client.publish("gridmitra/session/start", "{\"status\":\"CONNECTED\",\"otp_verified\":true}");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("EV Connected!");
    lcd.setCursor(0, 1);
    lcd.print("Charging Active");
    delay(1500);
  }

  if (isCharging) {
    int rawVoltage = analogRead(VOLTAGE_POT_PIN);
    int rawCurrent = analogRead(CURRENT_POT_PIN);

    float voltage = map(rawVoltage, 0, 4095, 200, 240);
    float current = map(rawCurrent, 0, 4095, 0, 160) / 10.0; 
    float powerKw = (voltage * current) / 1000.0;

unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 1000) {
      previousMillis = currentMillis;
      
      // Fixed demo increment: adds 1.0 kWh every second
      float kwhIncrement = 1.000; 
      
      totalKwh += kwhIncrement;
      totalBill = totalKwh * 4.50; // ₹4.50/kWh subsidized tariff

      char payload[128];
      snprintf(payload, sizeof(payload), "{\"kwh\":%.3f,\"kW\":%.2f,\"volts\":%.1f,\"amps\":%.1f,\"bill\":%.2f}", 
               totalKwh, powerKw, voltage, current, totalBill);

      client.publish("gridmitra/session/telemetry", payload);

      char apiPayload[160];
      snprintf(apiPayload, sizeof(apiPayload), "{\"delivered\":%.3f,\"voltage\":%.1f,\"current\":%.1f,\"power\":%.2f,\"elapsed_seconds\":%lu}",
           totalKwh, voltage, current, powerKw, millis() / 1000);
      postToApi("/api/hardware/reading", apiPayload);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(String(powerKw, 2) + "kW " + String(current, 1) + "A");
      lcd.setCursor(0, 1);
      lcd.print(String(totalKwh, 3) + "kWh Rs" + String(totalBill, 1));
    }

    if (buttonState && millis() - previousMillis > 3000) {
      isCharging = false;
      client.publish("gridmitra/session/stop", "{\"status\":\"COMPLETED\"}");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Charging Done");
      lcd.setCursor(0, 1);
      lcd.print("Bill: Rs" + String(totalBill, 2));
      delay(3000);
    }
  }
}