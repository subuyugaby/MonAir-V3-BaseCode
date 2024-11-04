// Libraries used
#include <WiFi.h>
#include <PubSubClient.h>  //Libreria para publicación y recepción de datos.
#include <Wire.h>          //Conexión de dispositivos I2C
#include "bsec.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>            // Para DS18B20
#include <DallasTemperature.h>  // Para DS18B20
#include "Adafruit_PM25AQI.h"   // Para sensor PMSA0031
#include <SPI.h>
#include <SD.h>

// WiFi Credentials
// Modify for assign your credentials
const char *ssid = "TIGO-C066";
const char *password = "4D99ED806661";

// MQTT Server
const char *mqtt_server = "galiot.galileo.edu";
// MQTT broker credentials
const char *user = "monair";
const char *passwd = "MONair2023";
const char *clientID = "airmonq_006570AA";  // Modify to assign to a database

//  Dashboard name
// Modify to assign to a dashboard and database
#define TEAM_NAME "airmon/006570AA"
#define NEOPIXEL_PIN 25
#define NUMPIXELS 16

//DO NOT MODIFY
// NTP conection variables
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Helper functions
Bsec iaqSensor;
String output, output2;

//  Data variables
int reconnectionCounter = 0;
double temp, ds18, hume, pres = 0;
double aqi, sAQI, AQIa = 0;
double CO2e, VOCe, gas, rssi, pm10, pm25 = 0;
char msg[50];
char msg_r[50];
char topic_name[250];

// Sensor DS28B20 functions
const int pinDatosDQ = 32;
OneWire oneWireObjeto(pinDatosDQ);
DallasTemperature sensorDS18B20(&oneWireObjeto);
// network variables
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// Sensor PMSA0031 functions
Adafruit_PM25AQI particulas = Adafruit_PM25AQI();
PM25_AQI_Data data;

//  NeoPixel declaration for signals
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);


#define CS_PIN 5  // Pin de selección del chip, ajusta según sea necesario

void setup() {
  Serial.begin(115200);

  //TEMPORAL SOOLO PARA PROBAR RECONEXIÓN
  pixels.setPixelColor(0, pixels.Color(218, 112, 214));
  pixels.show();
  delay(500);

  //WiFi setup
  setupWiFi();

  //MQTT setup
  setupMQTT();
  timeClient.begin();
  timeClient.setTimeOffset(-21600);

  SPI.begin(18, 19, 23, CS_PIN);
  // Inicializa la tarjeta SD
  if (!SD.begin(CS_PIN)) {
    Serial.println("No se pudo inicializar la tarjeta SD.");
  }
  Serial.println("Tarjeta SD inicializada correctamente.");

  // Manejo del primer archivo
  if (!SD.exists("/logs.txt")) {
    File archivo1 = SD.open("/logs.txt", FILE_WRITE);
    if (archivo1) {
      archivo1.println("Inicialización de logs");
      archivo1.close();
      Serial.println("Archivo logs.txt creado y datos escritos.");
    } else {
      Serial.println("Error al crear logs.txt.");
    }
  } else {
    Serial.println("El archivo logs.txt ya existe. Se continuará escribiendo en él.");
  }

  // Manejo del segundo archivo
  if (!SD.exists("/data.txt")) {
    File archivo2 = SD.open("/data.txt", FILE_WRITE);
    if (archivo2) {
      archivo2.println("temp,hume,pres,aqi,sAQI,AQIa,gas,CO2e,VOCe,rssi,pm25,pm10,ds18");
      archivo2.close();
      Serial.println("Archivo data.txt creado y datos escritos.");
    } else {
      Serial.println("Error al crear data.txt.");
    }
  } else {
    Serial.println("El archivo data.txt ya existe. Se continuará escribiendo en él.");
  }



  //BME680 Setup
  Wire.begin(21, 22);
  iaqSensor.begin(0x77, Wire);
  output = "BSEC library version: " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output);
  appendFile(SD, "/logs.txt", output.c_str());
  bsec_virtual_sensor_t sensorList[10] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,

  };
  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);

  // Print the header
  output = "************** Data Variables ************** \n\t-Timestamp [ms] \n\t-raw temperature [°C] \n\t-pressure [hPa] \n\t-raw relative humidity [%] \n\t-gas [Ohm] \n\t-IAQ \n\t-IAQ accuracy\n\t-temperature [°C] \n\t-relative humidity [%]\n\t-Static IAQ \n\t-CO2 equivalent \n\t-breath VOC equivalent ";
  Serial.println(output);
  appendFile(SD, "/logs.txt", output.c_str());
  delay(3000);

  /***** PMSA0031 sensor *********/
  Serial.println("");
  Serial.print("Checking PM25: ");
  if (!particulas.begin_I2C()) {  // connect to the sensor over I2C
    Serial.println("Could not find PM 2.5 sensor!");
    while (1)
      delay(10);
  } else {
    Serial.println("PM25 found!");
    appendFile(SD, "/logs.txt", "PM25 found!");
  }
}

void loop() {
  //checking WiFi conection
  if (WiFi.status() != WL_CONNECTED) {
    appendFile(SD, "/logs.txt", "LOOP: WiFi disconnected ");
    setupWiFi();
  }
  // if (!mqtt_client.connected()) {
  mqtt_client.loop();  //This should be called regularly to allow the client to process incoming messages and maintain its connection to the server.
  if (mqtt_client.state() != 0) {
    pixels.setPixelColor(0, pixels.Color(255, 255, 0));
    pixels.show();
    delay(500);
    appendFile(SD, "/logs.txt", "LOOP: MQTT disconnected ");
    reconnect();
  }

  timeClient.update();
  if (timeClient.getSeconds() % 15 == 0) {
    if ((WiFi.status() == WL_CONNECTED) && (mqtt_client.connect(clientID, user, passwd))) {
      String str69 = "Online";  // Se cambio revisar en Node-Red
      str69.toCharArray(msg, 50);
      mqtt_client.publish(getTopic("Online"), msg);
    }
  }

  //Checks posting_flag and post the data
  int posting_time = 2;
  if ((timeClient.getMinutes() % posting_time == 0) && (timeClient.getSeconds() == 00)) {
    Serial.print(String(posting_time) + " minutos publicando");
    postData();
    Serial.print("Datos publicados en MQTT Server");
    appendFile(SD, "/logs.txt", "Datos publicados en MQTT Server");
  }

  preHeatSensor();
}


void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);
  
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Error al abrir el archivo para agregar datos.");
    return;
  }
  if (file.print(message) && file.println()) { // Agregar salto de línea
    Serial.println("Mensaje agregado.");
  } else {
    Serial.println("Fallo al agregar el mensaje.");
  }
  file.close();
}



//This function prepares a sequence for the NeoPixel, you need to add rgb color and delay time as parameters.
void pixelSignals(int red, int green, int blue, int delay_time) {
  pixels.setPixelColor(0, pixels.Color(red, green, blue));
  pixels.show();
  delay(delay_time);
  pixels.clear();
  pixels.show();
  delay(delay_time);
}

//This functions connects to WiFi, it indicates with the NeoPixel when is connecting to some network and when finaly is connected
void setupWiFi() {
  //Inicializamos WiFi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  //Waiting for connection
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    appendFile(SD, "/logs.txt", "SETUP WIFI: Intento de Reconexión");
    reconnectionCounter += 1;
    delay(5000);
    if (reconnectionCounter > 250) {
      appendFile(SD, "/logs.txt", "SETUP WIFI: Reiniciando ESP32");
      ESP.restart();
    }
  }
  Serial.println("");
  Serial.println("************** Connection information **************");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("ESP Board MAC Address:  ");
  Serial.println(WiFi.macAddress());
  appendFile(SD, "/logs.txt", "SETUP WIFI: Conectado");
  //Indicates connection
  pixelSignals(0, 255, 0, 500);
  pixelSignals(0, 255, 0, 500);
}

// Helper function definitions
void publish(char *topic, char *payload) {
  Serial.println(topic_name);
  mqtt_client.publish(topic_name, payload);
}

//Example: TEAM_NAME/hume
char *getTopic(char *topic) {
  sprintf(topic_name, "%s/%s", TEAM_NAME, topic);
  return topic_name;
}

void reconnect() {
  // Loop until we're reconnected
  int cont = 1;
  while (!mqtt_client.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      setupWiFi();
    }
    pixels.setPixelColor(0, pixels.Color(255, 255, 0));
    pixels.show();
    delay(5000);
    appendFile(SD, "/logs.txt", "SETUP MQTT: Intento de Reconexión");
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect

    if (mqtt_client.connect(clientID, user, passwd)) {
      //mqtt_client.subscribe(getTopic("rgb"));
      appendFile(SD, "/logs.txt", "SETUP MQTT: Conectado");
      Serial.println("connected");
      pixelSignals(0, 255, 0, 500);
      pixelSignals(0, 255, 0, 500);
      pixelSignals(0, 255, 0, 500);

    } else {
      reconnectionCounter += 1;
      if (reconnectionCounter > 250) {
        appendFile(SD, "/logs.txt", "MQTT reconnection: Reiniciando ESP32");
        ESP.restart();
      }
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      cont += 1;
      if (cont > 150) {
        break;
      }
      delay(5000);
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    msg_r[i] = (char)payload[i];
  }
  msg_r[length] = 0;
  Serial.print("'");
  Serial.print(msg_r);
  Serial.println("'");
}

//This functions connects to MQTT
void setupMQTT() {
  pixels.setPixelColor(0, pixels.Color(255, 255, 0));
  pixels.show();
  delay(5000);
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
  reconnect();
}

//This function recolects the data from the sensors and post it
void postData() {
  String missing = "Datos no enviados: \n";
  String sent = "Datos enviados: \n";
  Serial.print("DESCONECTAR AQUÍ");
  if (WiFi.status() == WL_CONNECTED && mqtt_client.connect(clientID, user, passwd)) {
    Serial.println("");
    Serial.println("*** Cliente conectado a MQTT Server");

    
    //************ Posting Temperature************
    temp = iaqSensor.temperature;
    //Serial.println("Temperatura : " + String(temp));
    String(temp).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("temp"), msg)) ? sent += "\t- Temperatura enviada\n" : missing += "Temperatura\n";

    //************ Posting Humidity ************
    hume = iaqSensor.humidity;
    //Serial.println("Humedad : " + String(hume));
    String(hume).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("hume"), msg)) ? sent += "\t- Humedad enviada\n" : missing += "Humedad\n";

    //************ Posting Atmospheric Pressure ************
    pres = iaqSensor.pressure;
    //Serial.println("Presion Atmosferica : " + String(pres));
    String(pres).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("pres"), msg)) ? sent += "\t- Presion Atmosferica  enviada\n" : missing += "Presion Atmosferica \n";

    //************ Posting Index Air Quality ************
    aqi = iaqSensor.iaq;
    //Serial.println("Air Quality Index: " + String(aqi));
    String(aqi).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("aqi"), msg)) ? sent += "\t- Air Quality Index enviado\n" : missing += "Air Quality Index\n";

    //************ Posting Static Index Air Quality ************
    sAQI = iaqSensor.staticIaq;
    //Serial.println("Static Air Quality Index: " + String(sAQI));
    String(sAQI).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("sAQI"), msg)) ? sent += "\t- Static Air Quality Index enviado\n" : missing += "Index Air Quality\n";

    //************ Posting Index Air Quality Accurary ************
    AQIa = iaqSensor.iaqAccuracy;
    //Serial.println("Index Air Quality Accuracy : " + String(AQIa));
    String(AQIa).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("AQIa"), msg)) ? sent += "\t- Index Air Quality Accuracy enviado\n" : missing += "Index Air Quality Accuracy\n";

    //************ Posting Gas Resistence ************
    gas = (iaqSensor.gasResistance) / 1000;
    //Serial.println("Gas Resistance kOhms: " + String(gas));
    String(gas).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("gas"), msg)) ? sent += "\t- Gas Resistance kOhms enviado\n" : missing += "Gas Resistance kOhms\n";

    //************ Posting CO2 Equivalent ************
    CO2e = iaqSensor.co2Equivalent;
    //Serial.println("CO2 Equivalente : " + String(CO2e));
    String(CO2e).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("CO2e"), msg)) ? sent += "\t- CO2 Equivalente enviado\n" : missing += "CO2 Equivalente\n";

    //************ Posting VOC Equivalent ************
    VOCe = iaqSensor.breathVocEquivalent;
    //Serial.println("VOC Equivalente : " + String(VOCe));
    String(VOCe).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("VOCe"), msg)) ? sent += "\t- VOC Equivalente enviado\n" : missing += "VOC Equivalente \n";

    //************ Posting RSSI ************
    rssi = WiFi.RSSI();
    //Serial.println("Intensidad de Señal : " + String(rssi));
    String(rssi).toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("rssi"), msg)) ? sent += "\t- Intensidad de Señal enviado\n" : missing += "Intensidad de Señal\n";

    /*Posting particle measurements*/
    if (!particulas.read(&data)) {
      Serial.println("Could not read from AQI");
      delay(50);  // try again in a bit!
      return;
    } else {
      pm25 = data.pm25_env;
      String str11(pm25);
      str11.toCharArray(msg, 50);
      (mqtt_client.publish(getTopic("pm25"), msg)) ? sent += "\t- PM25 enviado\n" : missing += "PM25\n";

      pm10 = data.pm100_env;
      String str12(pm10);
      str12.toCharArray(msg, 50);
      (mqtt_client.publish(getTopic("pm10"), msg)) ? sent += "\t- PM10 enviado\n" : missing += "PM10\n";
    }

    sensorDS18B20.requestTemperatures();
    ds18 = sensorDS18B20.getTempCByIndex(0);
    String str13(ds18);
    str13.toCharArray(msg, 50);
    (mqtt_client.publish(getTopic("ds18"), msg)) ? sent += "\t- DS18 enviado\n" : missing += "DS18\n";

    Serial.println(sent);
    pixelSignals(0, 0, 255, 1000);
    appendFile(SD, "/logs.txt", "POST DATA: Datos enviados");
    if (missing != "Datos no sent: \n") {
      Serial.println(missing);
      Serial.println("Estado de la conexión MQTT Server: ");
      Serial.println(mqtt_client.state());
      String(missing).toCharArray(msg, 2000);
      (mqtt_client.publish(getTopic("info"), msg)) ? Serial.println("Log enviado.") : Serial.println("Fallo en envio de log");
    } else {
      String("Todos sent.").toCharArray(msg, 100);
      (mqtt_client.publish(getTopic("info"), msg)) ? Serial.println("Log enviado.") : Serial.println("Fallo en envio de log");
    }
  } else {
    mqtt_client.loop();
    appendFile(SD, "/logs.txt", "POST DATA: MQTT o WIFI desconectado");
    Serial.println("Cliente NO conectado a MQTT Server");
    Serial.print("Estado del error de conexión: ");
    Serial.println(mqtt_client.state());
    setupWiFi();
    Serial.println("Reconnect postdata else ");
    reconnect();
  }
}

void preHeatSensor() {
  unsigned long time_trigger = millis();
  if (iaqSensor.run()) {  // If new data is available
    output2 = String(time_trigger);

    output2 = String(time_trigger);
    output2 += ", " + String(iaqSensor.rawTemperature);
    output2 += ", " + String(iaqSensor.pressure);
    output2 += ", " + String(iaqSensor.rawHumidity);
    output2 += ", " + String(iaqSensor.gasResistance);
    output2 += ", " + String(iaqSensor.iaq);
    output2 += ", " + String(iaqSensor.iaqAccuracy);
    output2 += ", " + String(iaqSensor.temperature);
    output2 += ", " + String(iaqSensor.humidity);
    output2 += ", " + String(iaqSensor.staticIaq);
    output2 += ", " + String(iaqSensor.co2Equivalent);
    output2 += ", " + String(iaqSensor.breathVocEquivalent);
    //Serial.println(output2);
  }
}
