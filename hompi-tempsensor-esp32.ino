// INCLUDE
// WiFi / WebServer
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// time NTP
#include <time.h>

// Dallas temp sensor
#include <OneWire.h>
#include <DallasTemperature.h>

// LCD1306
#include "SSD1306Wire.h"

#include "hompi-tempsensor-esp32-config.h"

// WebServer
WebServer server(80);

// global runtime variables
char dateTime[18];
int tempUpdateTimer = 0;
float currentTempC = 0.0f;
String wifiIpAddress = "";

// SETUP
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(TEMP_BUS);
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

// Initialize the OLED display using Arduino Wire (i2c Address, SDA, SCL)
SSD1306Wire display(LCD_ADDRESS, LCD_SDA, LCD_SCL);

void setup(void) {
  // Initialize serial
  #ifdef debug
  Serial.begin(115200);
  #endif

  // configure NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Initialize LCD display
  display.init();
  display.flipScreenVertically();

  // Initialize DS18B20 sensor
  displayStatus("Initializing sensors...");
  sensors.begin();
}

void loop(void) {
  if (tempUpdateTimer <= 0) {
    if (connectWifi())
      updateLocalTime();
    tempUpdateTimer = 6000;
  }

  if (tempUpdateTimer % 1000 == 0) {
    sensors.requestTemperatures(); 
    currentTempC = sensors.getTempCByIndex(0);
    #ifdef debug
    Serial.println("Got temp: " + String(currentTempC,2));
    #endif
    displayTemperature();
  }
  tempUpdateTimer--;

  server.handleClient();
  delay(5); // allow the cpu to switch to other tasks
}

void displayStatus(String status) {
  display.clear();
  // the coordinates define the center of the text
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 22, status);

  // write the buffer to the display
  display.display();
}

void displayTemperature() {
  display.clear();
  // the coordinates define the center of the text
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 22, String(currentTempC, 1) + "° C");

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  if (wifiIpAddress != "") {
    // after a new successful connection, show IP
    display.drawString(128, 54, wifiIpAddress);
    wifiIpAddress = "";
  } else {
    // show date/time
    display.drawString(128, 54, dateTime);
  }

  // write the buffer to the display
  display.display();
}

// json sensor data
void sensorData() {
  String json = "{\"sensor\" : { ";
  json += "\"type\" : \"" + String(SENSOR_TYPE) + "\", ";
  json += "\"name\" : \"" + String(SENSOR_NAME) + "\", ";
  json += "\"desc\" : \"" + String(SENSOR_DESC) + "\", ";
  json += "\"temp\" : " + String(currentTempC, 2);
  json += " } }";
 
  #ifdef debug
  Serial.println("HTTP200: " + server.uri() + "\n" + json);
  #endif

  server.send(200, "application/json", json);
}

void http404() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  #ifdef debug
  Serial.println("HTTP404: " + message);
  #endif
  server.send(404, "text/plain", message);
}

String ipToString(const IPAddress& ipAddress) {
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3]);
}

void updateLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    #ifdef debug
    Serial.println("Failed to get NTP time");
    #endif
    return;
  }
  strftime(dateTime, 18, "%d-%m-%Y %H:%M", &timeinfo);
  #ifdef debug
  Serial.println(&timeinfo, "Got NTP time: %d-%m-%Y %H:%M");
  #endif
}

bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  // Network cleanup
  MDNS.end();
  WiFi.disconnect(true, true);
  wifiIpAddress = "";

  // Wifi connect
  displayStatus("Connecting to\n" + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  #ifdef debug
  Serial.println("");
  #endif

  // Wait for connection
  for (int retry = 0; WiFi.status() != WL_CONNECTED && retry < 20; retry++) {
    delay(500);
    displayStatus("Connecting to\n" + String(WIFI_SSID) + "\n" + String("....").substring(3 - (retry % 3)));
    #ifdef debug
    Serial.print(".");
    #endif
  }
  if (WiFi.status() != WL_CONNECTED) return false;

  wifiIpAddress = ipToString(WiFi.localIP());
  #ifdef debug
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(wifiIpAddress);
  #endif

  if (MDNS.begin(SENSOR_NAME)) {
    #ifdef debug
    Serial.println("MDNS responder started");
    #endif
  }

  displayStatus("Initializing HTTP server...");
  server.on("/", sensorData);
  server.onNotFound(http404);
  server.begin();
  #ifdef debug
  Serial.println("HTTP server started");
  #endif

  return true;
}
