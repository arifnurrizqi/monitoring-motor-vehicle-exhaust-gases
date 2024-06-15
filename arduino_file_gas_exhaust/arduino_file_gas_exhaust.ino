// Call Library
#include <Arduino.h>
#include <Wire.h>              // Library komunikasi I2C 
#include <LiquidCrystal_I2C.h> // Library modul I2C LCD
#include <MQUnifiedsensor.h>   // Library sensor MQ series
#include <WiFi.h>              // Library WiFi ESP32
#include <PubSubClient.h>      // Library untuk koneksi mqtt 

// Sensor Configuration
#define Board              "ESP-32"
#define Voltage_Resolution 3.3      // Resolusi tegangan untuk MQ-135
#define ADC_Bit_Resolution 12       // Jumlah ADC bit ESP32
#define RatioMQ135CleanAir 3.6      // Rasio untuk udara bersih MQ135

// Pin Configuration
#define PIN_MQ135          34       // MQ135 Analog Input Pin
#define PIN_BUZZER         5        // Buzzer Pin

// WiFi and MQTT Configuration
const char* ssid = "kecebong_tech";
const char* password = "qwertyuiop";
const char* mqtt_server = "broker.hivemq.com";              // Broker mqtt server
const int   mqtt_tcp_port = 1883;                           // TCP port untuk mqt server
const char* co_concentration_topic = "ta-anggi-ump20/co";   // Topic mqtt untuk gas CO
const char* nox_concentration_topic = "ta-anggi-ump20/nox"; // Topic mqtt untuk gas NOx

// LCD Configuration
LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4); // default address 0x27 tipe LCD 16x2 (16,2)

// MQ Sensor Objects
MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, PIN_MQ135, "MQ-135");

// WiFi and MQTT Client
WiFiClient espClient;
PubSubClient client(espClient);

// Pendefinisian Timing Variables
#define SENSOR_INTERVAL    2000     // Interval 2 detik untuk pembacaan sensor dan pengiriman ke broker
long lastMsg, lastReconnectAttempt, lastSensorRead = 0;

// Pendefinisian variabel lainnya
float R0_NOx, R0_CO;
String status;
bool isReadingCO = true; // Flag untuk menentukan gas yang sedang diukur
float nox_concentration, co_concentration, co_percentage; 

void setup() { // Program yang dijalankan sekali saat ESP32 menyala
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

  // Initialize MQ-135 for NOx
  MQ135.setRegressionMethod(1); 
  MQ135.setA(110.47); // Constants for NOx
  MQ135.setB(-2.862); // Constants for NOx
  MQ135.init();

  // Calibrasi Sensor untuk NOx (asumsikan itu berada di udara bersih saat startup)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Exhaust Gas Monitor");
  lcd.setCursor(0, 2);
  lcd.print("Calibrating NOx");
  Serial.print("Calibrating NOx, please wait.");
  float calcR0_NOx = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update();
    calcR0_NOx += MQ135.calibrate(RatioMQ135CleanAir);
    lcd.print(".");
    Serial.print(".");
    delay(100);
  }
  R0_NOx = calcR0_NOx / 10;
  Serial.println("MQ-135 NOx Calibration done!");

  // Initialize MQ-135 for CO
  MQ135.setA(605.18); // Constants for CO
  MQ135.setB(-3.937); // Constants for CO

  // Calibrasi Sensor untuk CO (asumsikan itu berada di udara bersih saat startup)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Exhaust Gas Monitor");
  lcd.setCursor(0, 2);
  lcd.print("Calibrating CO");
  Serial.print("Calibrating CO, please wait.");
  float calcR0_CO = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update();
    calcR0_CO += MQ135.calibrate(RatioMQ135CleanAir);
    lcd.print(".");
    Serial.print(".");
    delay(100);
  }
  R0_CO = calcR0_CO / 10;
  Serial.println("MQ-135 CO Calibration done!");

  // Menampilkan status kalibrasi sukses pada lcd
  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print("Exhaust Gas Monitor");
  lcd.setCursor(0, 2); 
  lcd.print("Calibration success");
  Serial.println("Calibration successful");
  delay(2500);
  lcd.clear();

  // Set initial R0 values
  MQ135.setR0(isReadingCO ? R0_CO : R0_NOx);
}

void loop() { // Program yang dijalankan berulang kali setelah selesai menjalankan program pada Void Setup()
  long now = millis();

  // Periksa apakah kadar gas buang diatas 50 PPM
  if (co_concentration >= 50.00 || nox_concentration >= 50.00){ // jika iya buzzer akan berbunyi dan memberikan status warning
    digitalWrite(PIN_BUZZER, HIGH);
    status = "Warning";
  } else { // jika tidak buzzer akan mati dan memberikan status safe
    digitalWrite(PIN_BUZZER, LOW);
    status = "Safe";
  }

  // Pembacaan sensor dengan interval 2 detik
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now; // mengupdate nilai variabel lastSensorRead

    // Update untuk sensor MQ135
    MQ135.update();

    if (isReadingCO) {
      // Memperbaharui nilai Variabel untuk CO
      MQ135.setA(605.18); // Constants for CO
      MQ135.setB(-3.937); // Constants for CO
      MQ135.setR0(R0_CO);
      co_concentration = MQ135.readSensor();

      // Konversi dari konsentrasi ppm ke persentase
      co_percentage = co_concentration / 10000; // dengan acuan bahwa 1% = 10000 ppm

      isReadingCO = false; // Switch to NOx for the next reading
    } else {
      // Memperbaharui nilai Variabel untuk NOx
      MQ135.setA(110.47); // Constants for NOx
      MQ135.setB(-2.862); // Constants for NOx
      MQ135.setR0(R0_NOx);
      float currentNOx = readAverageSensor(MQ135); // Rata-rata pembacaan
      nox_concentration = smoothValue(currentNOx, nox_concentration, 0.1); // Filter low-pass
      isReadingCO = true; // Switch to CO for the next reading
    }

    // menampilkan nilai pembacaan gas pada serial monitor
    Serial.print("CO : ");
    Serial.print(co_concentration);
    Serial.println(" ppm");
    Serial.print("CO Percent: ");
    Serial.print(co_percentage);
    Serial.println(" %");
    Serial.print("NOx Concentration: ");
    Serial.print(nox_concentration);
    Serial.println(" PPM");

    // menampilkan nilai pembacaan gas pada lcd 20x4
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

    // Publikasikan data ke MQTT server: hiveMQ
    if (client.connected()) {
      client.publish(co_concentration_topic, String(co_concentration).c_str());
      client.publish(nox_concentration_topic, String(nox_concentration).c_str());
    }
  }

  // Cek apakah esp terhubung dengan server MQTT
  if (!client.connected()) { // jika tidak terhubung jalankan program dibawahnya
    long now = millis();
    if (now - lastReconnectAttempt > 5000) { // mencoba menghubungkan ke server setiap 5 detik sekali
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

// Rata-rata dari 10 pembacaan
float readAverageSensor(MQUnifiedsensor &sensor) {
  float sum = 0;
  int readings = 10;
  for (int i = 0; i < readings; i++) {
    sensor.update();
    sum += sensor.readSensor();
    delay(100); // Delay antara pembacaan untuk stabilitas
  }
  return sum / readings;
}

// Filter low-pass untuk pembacaan sensor
float smoothValue(float currentValue, float previousValue, float smoothingFactor) {
  return (smoothingFactor * currentValue) + ((1.0 - smoothingFactor) * previousValue);
}

void setup_wifi() { // Fungsi untuk melakukan setup koneksi WiFi
  delay(10);
  Serial.println();
  lcd.setCursor(0, 0); 
  lcd.print("Connecting to ");
  lcd.println(ssid);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  
  // Loop untuk mencoba menghubungkan selama 15 detik (15000 milidetik) jika lebih dari 15 detik tetap tidak terhubung maka alat akan berjalan offline
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

// Fungsi untuk mencoba menghubungkan ESP32 dengan server MQTT
boolean reconnect() {
  if (client.connect("clientId-anggWBWump20")) { // jika id client esp32: "clientId-anggWBWump20" terhubung
    Serial.println("connected");
    return true;
  } else { // jika tidak terhubung akan mencoba lg dalam 5 detik
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    return false;
  }
}