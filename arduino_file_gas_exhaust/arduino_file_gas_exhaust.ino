#include <Arduino.h>
#include <Wire.h> // Library komunikasi I2C 
#include <LiquidCrystal_I2C.h> // Library modul I2C LCD
#include <MQUnifiedsensor.h>
#include <MQ7.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define Board           "ESP-32"
#define Voltage_Resolution 3.3
#define type            "MQ-135"
#define ADC_Bit_Resolution 12
#define RatioMQ135CleanAir  3.6
#define PIN_MQ135 34 // MQ135 Analog Input Pin
#define PIN_MQ7 35 // MQ7 Analog Input Pin
#define PIN_BUZZER 5 // Buzzer Pin
#define INTERVAL 5000
#define SENSOR_INTERVAL 2000 // Interval 2 detik untuk pembacaan sensor

const char* ssid = "ARNUR";
const char* password = "takonmama";
const char* mqtt_server = "broker.hivemq.com";
const char* co_consentration_topic = "ta-anggi-ump20/co";
const char* nox_consentration_topic = "ta-anggi-ump20/nox";

LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 16, 2); // default address 0x27 tipe LCD 16x2 (16,2)
MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, PIN_MQ135, type);
MQ7 mq7(PIN_MQ7, 5.0);
WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
long lastReconnectAttempt = 0;
long lastSensorRead = 0;

float nox_consentration, co_consentration;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  setup_wifi(); // inisialisasi koneksi wifi
  client.setServer(mqtt_server, 1883); // inisialisasi koneksi ke mqtt server
  
  // inisialisasi LCD:
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(1, 0); 
  lcd.print("CO & NOx Meter");

  MQ135.setRegressionMethod(1); 
  MQ135.setA(110.47); // Constants for NOx
  MQ135.setB(-2.862); // Constants for NOx

  MQ135.init();

  // Calibrate the sensor (assume it is in clean air at startup)
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

  lcd.setCursor(0, 0); 
  lcd.print("Sensor calibrated successfully.");
  Serial.println("Sensor calibrated successfully.");
  lcd.clear();
}

void loop() {
  long now = millis();

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
    lcd.print("CO  : ");
    lcd.setCursor(6, 0); 
    lcd.print(co_consentration);
    lcd.print(" ppm");
    lcd.setCursor(0, 1); 
    lcd.print("NOx : ");
    lcd.setCursor(6, 1); 
    lcd.print(nox_consentration);
    lcd.print(" ppm");

    // Publikasikan ke MQTT
    if (client.connected()) {
      client.publish(co_consentration_topic, String(co_consentration).c_str());
      client.publish(nox_consentration_topic, String(nox_consentration).c_str());
    }
  }

  // Periksa apakah kadar gas buang diatas 50 PPM
  if (co_consentration >= 50.00 || nox_consentration >= 50.00){
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }

  // Periksa koneksi WiFi dan MQTT di luar pembacaan sensor
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
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
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected, IP address: ");
  Serial.println(WiFi.localIP());
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
