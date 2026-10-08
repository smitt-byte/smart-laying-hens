#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>

// inisialisasi DHT22 dan relay
#include "DHT.h"
#define DHTPIN 27     
#define DHTTYPE DHT22 
#define RELAY_PIN1 16   //kipas1
#define RELAY_PIN2 17   //kipas2
#define RELAY_PIN3 18   //kipas3
#define RELAY_PIN4 23   //heater
#define RELAY_PIN5 19   // relay lampu penerang manual

//Wifi
#define LED_WIFI 2   // D2 pada ESP32

bool gasBahayaState = false;
bool gasSedangState = false;
bool suhuPanasState = false;

bool relay1State = false; // menyimpan kondisi relay1
bool relay2State = false; // menyimpan kondisi relay2
bool relay3State = false; // menyimpan kondisi relay3
bool relay4State = false; // menyimpan kondisi relay4
bool relay5State = false; // menyimpan kondisi relay4
bool dangerState = false; // menyimpan kondisi bahaya
DHT dht(DHTPIN, DHTTYPE);

// inisialisasi mq 137
#define RL 1       // Resistor RL (47K ohm) diganti (1k ohm)
#define m -0.263     // Slope
#define b 0.42       // Intercept
#define Ro 40        // Nilai Ro (hasil kalibrasi), didapatkan dari program cari_ro (20/24/40)
// Gunakan pin ADC ESP32 (contoh: GPIO34)
#define MQ_sensor 35 

//inisialisasi display oled
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Update these with values suitable for your network.
const char *ssid = "Cihuyy";
const char *password = "12345678";
//const char *ssid = "Zaky_LT 3";
//const char *password = "anindya4";
//const char *ssid = "Pandawa 2";
//const char *password = "PANDAWA2";
const char* mqtt_server = "61d5bee01bad436aa80e918b50bd41ca.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "hamid";
const char* mqtt_password = "Satudua3";

uint32_t last_ota_time = 0;

WiFiClientSecure espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;

//variabel kontrol suhu
// ====== SETPOINT HYSTERESIS ======

void setup_wifi() {
  // menyambungkan esp ke jaringan wifi
  delay (10);

  Serial.println();
  Serial.print("Menyambungkan ke ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_WIFI, HIGH);
    }

  randomSeed(micros());

  Serial.println("");
  Serial.println("Wifi Terhubung");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


//MQTT call back
void callback (char* topic, byte* payload, unsigned int length){

  String message = "";

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println(message);

   // ===== KONTROL LAMPU MANUAL =====
  if (String(topic) == "/esp32-mqtt/lampu_manual") {

    if (message == "ON" || message == "true") {
      relay5State = true;
      digitalWrite(RELAY_PIN5, HIGH);

      client.publish("/esp32-mqtt/status_lampu", "ON", true);

      Serial.println("Lampu manual ON");
    }

    else if (message == "OFF" || message == "false") {
      relay5State = false;
      digitalWrite(RELAY_PIN5, LOW);

      client.publish("/esp32-mqtt/status_lampu", "OFF", true);

      Serial.println("Lampu manual OFF");
    }
  }
}

//MQTT reconnect
void reconnect(){
  //melakukan looping hingga mqtt terkoneksi
  while (!client.connected()){
    Serial.println("attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");

      client.subscribe("/esp32/mqtt/in");
      client.subscribe("/esp32-mqtt/lampu_manual");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }      
   }
}

void tampilOLED(float suhu, float humi, float ppm) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(20, 0);
  display.println("LIVE MONITORING");

  display.setCursor(0, 15);
  display.print("Suhu : ");
  display.print(suhu);
  display.println(" C");

  display.setCursor(0, 30);
  display.print("Humi : ");
  display.print(humi);
  display.println(" %");

  display.setCursor(0, 45);
  display.print("NH3  : ");
  display.print(ppm);
  display.println(" ppm");

  display.display();
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  setup_wifi();
  dht.begin();
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_WIFI, LOW);

  pinMode(RELAY_PIN1, OUTPUT); // set relay sebagai output
  pinMode(RELAY_PIN2, OUTPUT);
  pinMode(RELAY_PIN3, OUTPUT);
  pinMode(RELAY_PIN4, OUTPUT);
  pinMode(RELAY_PIN5, OUTPUT);

  digitalWrite(RELAY_PIN1, LOW); // relay OFF awal
  digitalWrite(RELAY_PIN2, LOW); 
  digitalWrite(RELAY_PIN3, LOW);
  digitalWrite(RELAY_PIN4, LOW);
  digitalWrite(RELAY_PIN5, LOW);

  analogReadResolution(12);
  Serial.println("NH3 Gas Sensor Monitoring (ESP32)");


 ///////////============SETUP OTA===============///////////
  //ArduinoOTA.setHostname("fufufafa");
  ArduinoOTA.setPassword("antekasing");

    ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      if (millis() - last_ota_time > 500) {
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
        last_ota_time = millis();
      }
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

  ArduinoOTA.begin();

  Serial.println("Ready");

  // ===== CONNECT MQTT SAAT SETUP =====
  reconnect();
  client.loop();
  delay(500); // kasih waktu koneksi stabil

  // ===== KIRIM STATUS AWAL =====
  client.publish("/esp32-mqtt/dht22", "Normal", true);
  client.publish("/esp32-mqtt/mq137", "Normal", true);
  client.publish("/esp32-mqtt/heater", "OFF", true);
  client.publish("/esp32-mqtt/kipas1", "OFF", true);
  client.publish("/esp32-mqtt/kipas2", "OFF", true);
  client.publish("/esp32-mqtt/kipas3", "OFF", true);
  client.publish("/esp32-mqtt/status_lampu", "OFF", true);

  Serial.println("Status awal dikirim ke Node-RED");

  // ===== OLED SETUP =====
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal!");
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(15, 30);
  display.println("Smart Farm System");
  display.display();
  delay(2000);
}

void loop() {
  ArduinoOTA.handle();

  // ================= INDIKATOR WIFI =================
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH); // nyala terus
  } else {
    digitalWrite(LED_WIFI, LOW);  // mati
  }

///////////////////////baca DHT22/////////////////////////
  float h = dht.readHumidity();
  float t = dht.readTemperature();

///////////////////////baca MQ-137/////////////////////////
  float VRL;
  float Rs;
  float ratio;

  int adcValue = analogRead(MQ_sensor);

  // Konversi ADC ke tegangan (ESP32 pakai 3.3V & 12-bit)
  VRL = adcValue * (3.3 / 4095.0);
  // Hindari pembagian nol
  if (VRL == 0) {
    Serial.println("Error: VRL = 0");
    delay(1000);
    return;
  }
  // Hitung resistansi sensor
  Rs = ((3.3 * RL) / VRL) - RL;
  // Hitung rasio
  ratio = Rs / Ro;
  // Hitung ppm
  float ppm = pow(10, ((log10(ratio) - b) / m));
  // Output ke Serial Monitor
  Serial.print("ADC: ");
  Serial.print(adcValue);
  Serial.print(" | Voltage: ");
  Serial.print(VRL);
  Serial.print(" V | NH3: ");
  Serial.print(ppm);
  Serial.println(" ppm");
  delay (1000);

  ////----------display oled--------------/////
  tampilOLED(t, h, ppm);
  
/////////////////////////------MQTT------////////////////////////////
//Membaca dan mengirim data dht22 ke nodered
   if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
  
      // kirim suhu
    char tempString[8];
    dtostrf(t, 1, 2, tempString);
    Serial.print("Temperature: ");
    Serial.println(tempString);
    client.publish("/esp32-mqtt/temp", tempString);
   
    // kirim kelembapan
    char humString[8];
    dtostrf(h, 1, 2, humString);
    Serial.print("Humidity: ");
    Serial.println(humString);
    client.publish("/esp32-mqtt/humi", humString);

    // Kirim PPM
    char ppmString[8];
    dtostrf(ppm, 1, 2, ppmString);
    client.publish("/esp32-mqtt/ppm", ppmString);
  }

    // ===== HYSTERESIS GAS SEDANG =====
    // ON  ketika ppm >= 5
    // OFF ketika ppm <= 3
    if (ppm >= 5.0) {
      gasSedangState = true;
    }
    else if (ppm <= 3.0) {
      gasSedangState = false;
    }

    // ===== HYSTERESIS SUHU PANAS =====
    // ON  ketika suhu >= 32.5
    // OFF ketika suhu <= 31.5
    if (t >= 32.5) {
      suhuPanasState = true;
    }
    else if (t <= 31.5) {
      suhuPanasState = false;
    }
    // ===== LAMP (SUHU RENDAH) =====
    if (t <= 28.4 && !relay4State) { /// 28
      relay4State = true;
      digitalWrite(RELAY_PIN4, HIGH);
      client.publish("/esp32-mqtt/heater", "ON");
      Serial.println("Heater ON");
    } 
    else if (t >= 30.0 && relay4State) {/// 30/29
      relay4State = false;
      digitalWrite(RELAY_PIN4, LOW);
      client.publish("/esp32-mqtt/heater", "OFF");
      Serial.println("Heater OFF");
    }

    // ===== GABUNGAN KONDISI RELAY =====
    // Relay akan ON jika minimal salah satu kondisi masih membutuhkan relay tersebut.

    // Kondisi 1: Gas bahaya
    // ppm >= 20     : R1, R2, R3 ON
    // turun <= 15   : keluar dari bahaya
    bool r1GasBahaya = gasBahayaState;
    bool r2GasBahaya = gasBahayaState;
    bool r3GasBahaya = gasBahayaState;

    // Kondisi 2: Gas sedang
    // ppm >= 5      : R2, R3 ON
    // turun <= 3    : R2, R3 OFF
    bool r2GasSedang = gasSedangState;
    bool r3GasSedang = gasSedangState;

    // Kondisi 3: Suhu panas
    // suhu >= 32.5  : R1, R2 ON
    // turun <= 31.5 : R1, R2 OFF
    bool r1SuhuPanas = suhuPanasState;
    bool r2SuhuPanas = suhuPanasState;

    // ===== OUTPUT FINAL RELAY =====
    // Menggunakan OR supaya relay tidak mati jika masih dibutuhkan kondisi lain.
    relay1State = r1GasBahaya || r1SuhuPanas;
    relay2State = r2GasBahaya || r2GasSedang || r2SuhuPanas;
    relay3State = r3GasBahaya || r3GasSedang;

    // ===== OUTPUT =====
    digitalWrite(RELAY_PIN1, relay1State ? HIGH : LOW);
    digitalWrite(RELAY_PIN2, relay2State ? HIGH : LOW);
    digitalWrite(RELAY_PIN3, relay3State ? HIGH : LOW);
  

    //================= KIRIM indikator led MQTT =================
  
  client.publish("/esp32-mqtt/kipas1", relay1State ? "ON" : "OFF", true);
  client.publish("/esp32-mqtt/kipas2", relay2State ? "ON" : "OFF", true);
  client.publish("/esp32-mqtt/kipas3", relay3State ? "ON" : "OFF", true);

  //======kirim status di node red=========//
  //dht
   if (t >= 32.5) {
      client.publish("/esp32-mqtt/dht22", "Tinggi");
    } 
  else if (t >= 28.4 ) {
      client.publish("/esp32-mqtt/dht22", "Normal");
  }
   else {
      client.publish("/esp32-mqtt/dht22", "Rendah");
   }
  
  //mq137
  if (ppm >= 20) {
      client.publish("/esp32-mqtt/mq137", "Tinggi");
    } 
    else if(ppm >= 5.0 && ppm < 20.0){
      client.publish("/esp32-mqtt/mq137", "Sedang");
    }
    else {
      client.publish("/esp32-mqtt/mq137", "Normal");
    }

  

  // kirim google sheet
static unsigned long lastSend = 0;
  if (millis() - lastSend >= 60000) {

    lastSend = millis();

    String payload = "{";
    payload += "\"temp\":" + String(t,1) + ",";
    payload += "\"humi\":" + String(h,1) + ",";
    payload += "\"ppm\":" + String(ppm,2);
    payload += "}";

    client.publish("/esp32-mqtt/data", payload.c_str());

    Serial.println(payload);
  }

  // ================= DEBUG =================
  Serial.print("R1: "); Serial.print(relay1State);
  Serial.print(" | R2: "); Serial.print(relay2State);
  Serial.print(" | R3: "); Serial.print(relay3State);
  Serial.print(" | R4: "); Serial.println(relay4State);

delay (1000);
}


