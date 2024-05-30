#include <Arduino.h>
#include <Wire.h>              // Library komunikasi I2C 
#include <LiquidCrystal_I2C.h> // Library modul I2C LCD
#include <MQUnifiedsensor.h>   // Library sensor MQ series
#include <MQ7.h>               // Library sensor MQ7
#include <WiFi.h>              // Library WiFi ESP32
#include <PubSubClient.h>      // Library untuk koneksi mqtt 
#include <WiFiManager.h>       // Library WiFiManager

#define Board              "ESP-32"
#define Voltage_Resolution 3.3      // Resolusi tegangan untuk MQ-135
#define type               "MQ-135" // Jenis sensor MQ-135
#define ADC_Bit_Resolution 12       // Jumlah ADC bit ESP32
#define RatioMQ135CleanAir 3.6      // Rasio untuk udara bersih
#define PIN_MQ135          34       // MQ135 Analog Input Pin
#define PIN_MQ7            35       // MQ7 Analog Input Pin
#define PIN_BUZZER         5        // Buzzer Pin
#define SENSOR_INTERVAL    2000     // Interval 2 detik untuk pembacaan sensor dan pengiriman ke broker

const char* mqtt_server = "broker.hivemq.com";              // Broker mqtt server
const int mqtt_tcp_port = 1883;                         // TCP port untuk mqt server
const char* co_consentration_topic = "ta-anggi-ump20/co";   // Topic mqtt untuk gas CO
const char* nox_consentration_topic = "ta-anggi-ump20/nox"; // Topic mqtt untuk gas NOx

LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4); // default address 0x27 tipe LCD 16x2 (16,2)
MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, PIN_MQ135, type);
MQ7 mq7(PIN_MQ7, 5.0);
WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wm;

// Pendefinisian variabel
long lastMsg, lastReconnectAttempt, lastSensorRead = 0;
float nox_consentration, co_consentration;
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
  lcd.createChar(0, wifiSymbol); // Membuat karakter khusus di lokasi 0
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
    // lcd.clear();
    // lcd.setCursor(0, 0);
    // lcd.print("Hubungkan ke WiFi:");
    // lcd.setCursor(0, 1);
    // lcd.print("Exhaust Gas Monitor");
    // lcd.setCursor(0, 2);
    // lcd.print("dan buka 192.168.4.1");
    // lcd.setCursor(0, 3);
    // lcd.print("pada browser...");
    // delay(5000);

    // lcd.clear();
    // lcd.setCursor(0, 0);
    // lcd.print("Atau Tunggu 1 menit");
    // lcd.setCursor(0, 1);
    // lcd.print("untuk memasuki mode");
    // lcd.setCursor(0, 2);
    // lcd.print("Offline");

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

  MQ135.setRegressionMethod(1); 
  MQ135.setA(110.47); // Constants for NOx
  MQ135.setB(-4.862); // Constants for NOx

  MQ135.init();

  // Calibrate the sensor (assume it is in clean air at startup)
   lcd.clear();
  lcd.setCursor(0, 1); 
  lcd.print("Calibrating process");
  Serial.print("Calibrating please wait.");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update();
    calcR0 += MQ135.calibrate(RatioMQ135CleanAir);
    Serial.print(".");
  }
  MQ135.setR0(calcR0 / 10);
  Serial.println(" done!.");

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
  if (co_consentration >= 50.00 || nox_consentration >= 50.00){
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
    nox_consentration = MQ135.readSensor();
    co_consentration = mq7.getPPM();

    Serial.print("CO : ");
    Serial.print(co_consentration);
    Serial.println(" PPM");
    Serial.print("NOx Concentration: ");
    Serial.print(nox_consentration);
    Serial.println(" PPM");

    lcd.clear();
    lcd.setCursor(0, 0); 
    lcd.print("Exhaust Gas Monitor");
    lcd.setCursor(0, 1); 
    lcd.print(" CO    : ");
    lcd.setCursor(9, 1); 
    lcd.print(co_consentration);
    lcd.print(" ppm");
    lcd.setCursor(0, 2); 
    lcd.print(" NOx   : ");
    lcd.setCursor(9, 2); 
    lcd.print(nox_consentration);
    lcd.print(" ppm");
    lcd.setCursor(0, 3); 
    lcd.print("Status : ");
    lcd.print(status);
    cekWiFI();

    // Publikasikan ke MQTT
    if (client.connected()) {
      client.publish(co_consentration_topic, String(co_consentration).c_str());
      client.publish(nox_consentration_topic, String(nox_consentration).c_str());
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
