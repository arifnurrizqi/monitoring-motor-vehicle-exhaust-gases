// Call Library
#include <Arduino.h>
#include <Wire.h>              // Library komunikasi I2C 
#include <LiquidCrystal_I2C.h> // Library modul I2C LCD
#include <MQUnifiedsensor.h>   // Library sensor MQ series
#include <WiFi.h>              // Library WiFi ESP32
#include <PubSubClient.h>      // Library untuk koneksi mqtt 
#include <WiFiManager.h>       // Library WiFiManager

// Sensor Configuration
#define Board              "ESP-32"
#define Voltage_Resolution 3.3      // Resolusi tegangan untuk MQ-135
#define ADC_Bit_Resolution 12       // Jumlah ADC bit ESP32
#define RatioMQ135CleanAir 3.6      // Rasio untuk udara bersih MQ135
#define RatioMQ7CleanAir   27.0     // Rasio untuk udara bersih MQ7

// Pin Configuration
#define PIN_MQ135          34       // MQ135 Analog Input Pin
#define PIN_MQ7            35       // MQ7 Analog Input Pin
#define PIN_BUZZER         5        // Buzzer Pin

// MQTT Configuration
const char* mqtt_server = "broker.hivemq.com";              // Broker mqtt server
const int   mqtt_tcp_port = 1883;                         // TCP port untuk mqt server
const char* co_concentration_topic = "ta-anggi-ump20/co";   // Topic mqtt untuk gas CO
const char* nox_concentration_topic = "ta-anggi-ump20/nox"; // Topic mqtt untuk gas NOx

// LCD Configuration
LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4); // default address 0x27 tipe LCD 16x2 (16,2)

// MQ Sensor Objects
MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, PIN_MQ135, "MQ-135");
MQUnifiedsensor MQ7(Board, Voltage_Resolution, ADC_Bit_Resolution, PIN_MQ7, "MQ-7");

// WiFi and MQTT Client
WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wm;

// Pendefinisian Timing Variables
#define SENSOR_INTERVAL    2000     // Interval 2 detik untuk pembacaan sensor dan pengiriman ke broker
long lastMsg, lastReconnectAttempt, lastSensorRead = 0;

// Pendefinisian variabel lainnya
float nox_concentration, co_concentration;
String status;
String message = "            Hubungkan ke WiFi 'Exhaust Gas Monitor' dan buka 192.168.4.1 pada browser, Atau Tunggu 1 menit untuk masuk mode Offline";
int messageLength = message.length();
int scrollPosition = 0;
bool wifiConnected = false;
unsigned long portalTimeout = 60; // Timeout portal dalam 60 detik
byte wifiSymbol[8] = { // Definisi karakter khusus untuk simbol WiFi
  B00100,
  B01010,
  B10001,
  B00100,
  B01010,
  B00000,
  B00100,
  B00000
};

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);

  // inisialisasi LCD:
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print("Exhaust Gas Monitor");
  lcd.setCursor(0, 1); 
  lcd.print("Tugas Akhir:");
  lcd.setCursor(0, 2); 
  lcd.print("Anggi Wibowo");
  lcd.setCursor(0, 3); 
  lcd.print("NPM : 2003030039");
  delay(5000);

  // inisialisasi koneksi wifi dengen wifimanager
  wm.setConfigPortalBlocking(false);  // Inisialisasi WiFiManager tanpa blocking

  // Coba untuk auto-connect ke jaringan WiFi yang pernah disimpan
  if (!wm.autoConnect("Exhaust Gas Monitor", "1234567890")) {
    unsigned long startTime = millis();
    Serial.println("Hubungkan ke WiFi 'Exhaust Gas Monitor' dan buka 192.168.4.1 pada browser, Atau Tunggu 1 menit untuk memasuki mode Offline");
    
    wm.startConfigPortal("Exhaust Gas Monitor", "1234567890");
    // Loop untuk menunggu hingga 60 detik atau koneksi berhasil
    while (millis() - startTime < portalTimeout*1000) { 
      wm.process();

      // Mengatur posisi awal untuk menampilkan teks berjalan
      lcd.setCursor(0, 3);

      // Menampilkan sebagian pesan tergantung pada posisi scroll
      for (int i = 0; i < 20; i++) {
        if ((scrollPosition + i) < messageLength) {
          lcd.print(message[scrollPosition + i]);
        } else {
          lcd.print(" ");
        }
      }

      // Mengatur posisi scroll
      scrollPosition++;
      if (scrollPosition > messageLength) {
        scrollPosition = 0;
      }

      // Menunggu sebelum menggeser teks lagi
      delay(220);

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi during portal!");
        wifiConnected = true;
        break;
      }
    }
    // Jika tidak terhubung setelah 60 detik, matikan portal
    if (WiFi.status() != WL_CONNECTED) {
      wm.stopConfigPortal();
      Serial.println("Config portal timeout, running offline");
    }
  } else {
    Serial.println("Connected to WiFi!");
    wifiConnected = true;
  }

  client.setServer(mqtt_server, mqtt_tcp_port); // inisialisasi koneksi ke mqtt server

  // Initialize MQ-135 for NOx
  MQ135.setRegressionMethod(1); 
  MQ135.setA(110.47); // Constants for NOx
  MQ135.setB(-4.862); // Constants for NOx
  MQ135.init();

  // Calibrate the sensor (assume it is in clean air at startup)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Exhaust Gas Monitor");
  lcd.setCursor(0, 2);
  lcd.print("Calibrating process");
  Serial.print("Calibrating please wait.");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update();
    calcR0 += MQ135.calibrate(RatioMQ135CleanAir);
    lcd.print(".");
    Serial.print(".");
    delay(100);
  }
  MQ135.setR0(calcR0 / 10);
  Serial.println("MQ-135 Calibration done!");

  // Initialize MQ-7 for CO
  MQ7.setRegressionMethod(1); 
  MQ7.setA(99.042); // Constants for CO
  MQ7.setB(-1.518); // Constants for CO
  MQ7.init();
  float calcR0_MQ7 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ7.update();
    calcR0_MQ7 += MQ7.calibrate(RatioMQ7CleanAir);
    Serial.print(".");
  }
  MQ7.setR0(calcR0_MQ7 / 10);
  Serial.println("MQ-7 Calibration done!");

  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print("Exhaust Gas Monitor");
  lcd.setCursor(0, 2); 
  lcd.print("Calibration success");
  Serial.println("Calibration successful");
  delay(2500);
  lcd.clear();
}

void loop() {
  long now = millis();

  // Periksa apakah kadar gas buang diatas 50 PPM
  if (co_concentration >= 50.00 || nox_concentration >= 50.00){
    digitalWrite(PIN_BUZZER, HIGH);
    status = "Warning";
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    status = "Safe";
  }

  // Pembacaan sensor dengan interval 2 detik
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    MQ135.update();
    MQ7.update();

    float nox_concentration = MQ135.readSensor();
    float co_concentration = MQ7.readSensor();

    Serial.print("CO : ");
    Serial.print(co_concentration);
    Serial.println(" PPM");
    Serial.print("NOx Concentration: ");
    Serial.print(nox_concentration);
    Serial.println(" PPM");

    lcd.clear();
    lcd.setCursor(0, 0); 
    lcd.print("Exhaust Gas Monitor");
    lcd.setCursor(0, 1); 
    lcd.print(" CO    : ");
    lcd.setCursor(9, 1); 
    lcd.print(co_concentration);
    lcd.print(" ppm");
    lcd.setCursor(0, 2); 
    lcd.print(" NOx   : ");
    lcd.setCursor(9, 2); 
    lcd.print(nox_concentration);
    lcd.print(" ppm");
    lcd.setCursor(0, 3); 
    lcd.print("Status : ");
    lcd.print(status);
    cekWiFI();

    // Publikasikan ke MQTT
    if (client.connected()) {
      client.publish(co_concentration_topic, String(co_concentration).c_str());
      client.publish(nox_concentration_topic, String(nox_concentration).c_str());
    }
  }

  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop();
  }

  delay(100);
}

void cekWiFI() {
  // Cek status koneksi WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected!");
    lcd.setCursor(19, 3); 
    lcd.print("x");
    wifiConnected = false;
  } else {
    if (wifiConnected) {
      Serial.println("WiFi connected!");
      lcd.setCursor(19, 3); 
      lcd.write(byte(0));
      wifiConnected = true;
    }
  }
}



boolean reconnect() {
  if (client.connect("clientId-anggWBWump20")) {
    Serial.println("connected");
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    return false;
  }
}
