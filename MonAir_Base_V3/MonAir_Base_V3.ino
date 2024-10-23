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

// WiFi Credentials
// Modify for assign your credentials
const char *ssid = "galileo";
const char *password = "";

// MQTT Server
const char *mqtt_server = "galiot.galileo.edu";
// MQTT broker credentials
const char *user = "monair";
const char *passwd = "MONair2023";
const char *clientID = "airmonq_006570A4";  // Modify to assign to a database

//  Dashboard name
// Modify to assign to a dashboard and database
#define TEAM_NAME "airmon/006570A4"
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

/* This flag controls that the station is sending data in the stablished time 
    - It is TRUE when the stablished time has passed, it allowes to send data
    - FALSE: when data where sent
*/
boolean posting_flag = true;

void setup() {
  Serial.begin(115200);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);

  //WiFi setup
  setupWiFi();
  Serial.print("ESP Board MAC Address:  ");
  Serial.println(WiFi.macAddress());

  //MQTT setup
  setupMQTT();
  timeClient.begin();
  timeClient.setTimeOffset(-21600);

  //BME680 Setup
  Wire.begin(21, 22);
  iaqSensor.begin(0x77, Wire);
  output = "BSEC library version: " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output);
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
  }
}

void loop() {
  digitalWrite(15, LOW);
  //checking WiFi conection
  if (WiFi.status() != WL_CONNECTED) {
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    setupWiFi();
  }
  if (!mqtt_client.connected()) {
    reconnect();
  }


  timeClient.update();
  if (timeClient.getSeconds() == 15) {
      if ((WiFi.status() != WL_CONNECTED) && (!mqtt_client.connect(clientID, user, passwd))) {
        String str69 = "Estacion en línea";
        str69.toCharArray(msg, 50);
        mqtt_client.publish(getTopic("Online"), msg);
      }
    }


  //if ((timeClient.getMinutes() % 5 == 00) && (timeClient.getSeconds() == 00) && posting_flag)
  //Checks posting_flag and post the data

  int posting_time = 2;
  if ((timeClient.getMinutes() % posting_time == 0) && (timeClient.getSeconds() == 00) && posting_flag) {
    Serial.print(String(posting_time) + " minutos publicando");
    digitalWrite(15, HIGH);  //
    delay(5000);
    postData();
    Serial.print("Datos publicados en MQTT Server: ");
    pixelSignals(0, 0, 255, 1000);
    posting_flag = false;
    digitalWrite(15, LOW);
    // Serial.println("Getting into deep sleep mode");
    //delay(5000);
    //esp_deep_sleep((posting_time - 1)*60000000-5000000);
  } else if (timeClient.getSeconds() != 00) {
    posting_flag = true;
  }
  preHeatSensor();
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
    Serial.print("WiFi no conectado");
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
  }
  Serial.println("");
  Serial.println("************** Connection information **************");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
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
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect

    if (mqtt_client.connect(clientID, user, passwd)) {
      //mqtt_client.subscribe(getTopic("rgb"));
      Serial.println("connected");
      pixelSignals(0, 255, 0, 500);
      pixelSignals(0, 255, 0, 500);
      pixelSignals(0, 255, 0, 500);

    } else {

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
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
}

//This function recolects the data from the sensors and post it
void postData() {
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
  delay(100);
  String missing = "Datos no enviados: \n";
  String sent = "Datos enviados: \n";
  if (mqtt_client.connect(clientID, user, passwd)) {
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
    Serial.println("Cliente NO conectado a MQTT Server");
    Serial.print("Estado del error de conexión: ");
    Serial.println(mqtt_client.state());
    setupWiFi();
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
