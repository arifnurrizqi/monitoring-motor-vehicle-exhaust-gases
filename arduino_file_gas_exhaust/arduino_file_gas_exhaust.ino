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

const char* ssid = "kecebong_tech";
const char* password = "qwertyuiop";
const char* mqtt_server = "broker.hivemq.com";              // Broker mqtt server
const int   mqtt_tcp_port = 1883;                         // TCP port untuk mqt server
const char* co_concentration_topic = "ta-anggi-ump20/co";   // Topic mqtt untuk gas CO
const char* nox_concentration_topic = "ta-anggi-ump20/nox"; // Topic mqtt untuk gas NOx

LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4); // default address 0x27 tipe LCD 16x2 (16,2)
MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, PIN_MQ135, type);
MQ7 mq7(PIN_MQ7, 5.0);
WiFiClient espClient;
PubSubClient client(espClient);

// Pendefinisian variabel
long lastMsg, lastReconnectAttempt, lastSensorRead = 0;
float nox_concentration, co_concentration, co_percentage;
String status;

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

  setup_wifi(); // inisialisasi koneksi wifi

  client.setServer(mqtt_server, mqtt_tcp_port); // inisialisasi koneksi ke mqtt server

  MQ135.setRegressionMethod(1); 
  MQ135.setA(110.47); // Constants for NOx
  MQ135.setB(-2.862); // Constants for NOx

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
    nox_concentration = MQ135.readSensor();
    co_concentration = mq7.getPPM();

    // Konversi dari konsentrasi ppm ke persentase
    co_percentage = co_concentration / 10000; // dengan acuan bahwa 1% = 10000 ppm

    Serial.print("CO : ");
    Serial.print(co_concentration);
    Serial.println(" PPM");
    Serial.print("CO : ");
    Serial.print(co_percentage);
    Serial.println(" %");
    Serial.print("NOx Concentration: ");
    Serial.print(nox_concentration);
    Serial.println(" PPM");

    lcd.clear();
    lcd.setCursor(0, 0); 
    lcd.print("Exhaust Gas Monitor");
    lcd.setCursor(0, 1); 
    lcd.print(" CO    : ");
    lcd.setCursor(9, 1); 
    lcd.print(co_percentage);
    lcd.print(" %");
    lcd.setCursor(0, 2); 
    lcd.print(" NOx   : ");
    lcd.setCursor(9, 2); 
    lcd.print(nox_concentration);
    lcd.print(" ppm");
    lcd.setCursor(0, 3); 
    lcd.print("Status : ");
    lcd.print(status);

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

void setup_wifi() {
  delay(10);
  Serial.println();
  lcd.setCursor(0, 0); 
  lcd.print("Connecting to ");
  lcd.println(ssid);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  
  // Loop untuk mencoba menghubungkan selama 15 detik (15000 milidetik)
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    lcd.setCursor(0, 2); 
    lcd.print("WiFi connected");
    lcd.setCursor(0, 3); 
    lcd.print("IP : ");
    lcd.println(WiFi.localIP());
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("Failed to connect to WiFi within 15 seconds.");
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
