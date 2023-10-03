#include <Arduino.h>

#include <WebSocketsServer.h>   // https://github.com/Links2004/arduinoWebSockets
#include <esp-fs-webserver.h>   // https://github.com/cotestatnt/esp-fs-webserver


#include <FS.h>
#include <LittleFS.h>
#define FILESYSTEM LittleFS

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif


// In order to set SSID and password open the /setup webserver page
// const char* ssid;
// const char* password;

bool apMode = false;
char const* hostname = "fsbrowser";

// Test "config" values
String option1 = "Test option String";
uint32_t option2 = 1234567890;
uint8_t ledPin = LED_BUILTIN;

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
struct tm Time;

#ifdef ESP8266
  ESP8266WebServer server(80);
#elif defined(ESP32)
  WebServer server(80);
#endif

FSWebServer myWebServer(FILESYSTEM, server);
WebSocketsServer webSocket = WebSocketsServer(81);

////////////////////////////////   WebSocket Handler  /////////////////////////////
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
        DebugPrintf("[%u] Disconnected!\n", num);
        break;
    case WStype_CONNECTED:
        {
          IPAddress ip = webSocket.remoteIP(num);
          DebugPrintf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
          // send message to client
          webSocket.sendTXT(num, "{\"Connected\": true}");
        }
        break;
    case WStype_TEXT:
        DebugPrintf("[%u] get Text: %s\n", num, payload);
        // send message to client
        // webSocket.sendTXT(num, "message here");

        // send data to all connected clients
        // webSocket.broadcastTXT("message here");
        break;
    case WStype_BIN:
        DebugPrintf("[%u] get binary length: %u\n", num, length);
        break;
    default:
        break;
  }

}

////////////////////////////////  NTP Time  /////////////////////////////////////
void getUpdatedtime(const uint32_t timeout)
{
  uint32_t start = millis();
  DebugPrint("Sync time...");
  while (millis() - start < timeout  && Time.tm_year <= (1970 - 1900)) {
    time_t now = time(nullptr);
    Time = *localtime(&now);
    delay(5);
  }
  DebugPrintln(" done.");
}


////////////////////////////////  Filesystem  /////////////////////////////////////////
void startFilesystem(){
  // FILESYSTEM INIT
  if ( FILESYSTEM.begin()){
    File root = FILESYSTEM.open("/", "r");
    File file = root.openNextFile();
    while (file){
      const char* fileName = file.name();
      size_t fileSize = file.size();
      DebugPrintf("FS File: %s, size: %lu\n", fileName, (long unsigned)fileSize);
      file = root.openNextFile();
    }
    DebugPrintln();
  }
  else {
    DebugPrintln("ERROR on mounting filesystem. It will be formmatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
}


////////////////////  Load and save application configuration from filesystem  ////////////////////
void saveApplicationConfig(){
  StaticJsonDocument<1024> doc;
  File file = FILESYSTEM.open("/config.json", "w");
  doc["Option 1"] = option1;
  doc["Option 2"] = option2;
  doc["AP mode"] = apMode;
  doc["LED Pin"] = ledPin;
  serializeJsonPretty(doc, file);
  file.close();
  delay(1000);
  ESP.restart();
}

void loadApplicationConfig() {
  StaticJsonDocument<1024> doc;
  File file = FILESYSTEM.open("/config.json", "r");
  if (file) {
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error) {
      DebugPrintln(F("Deserializing JSON.."));
      apMode = doc["AP mode"];
      option1 = doc["Option 1"].as<String>();
      option2 = doc["Option 2"];
      ledPin = doc["LED Pin"];
    }
    else {
      DebugPrintln(F("Failed to deserialize JSON. File could be corrupted"));
      DebugPrintln(error.c_str());
      saveApplicationConfig();
    }
  }
  else {
    saveApplicationConfig();
    DebugPrintln(F("New file created with default values"));
  }
}

////////////////////////////  HTTP Request Handlers  ////////////////////////////////////
void handleLed() {
  WebServerClass* webRequest = myWebServer.getRequest();

  // http://xxx.xxx.xxx.xxx/led?val=1
  if(webRequest->hasArg("val")) {
    int value = webRequest->arg("val").toInt();
    digitalWrite(ledPin, value);
  }

  String reply = "LED is now ";
  reply += digitalRead(ledPin) ? "OFF" : "ON";
  webRequest->send(200, "text/plain", reply);
}



void setup(){

#if DEBUG_MODE_WS
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
#endif

  // FILESYSTEM INIT
  startFilesystem();

  // Load configuration (if not present, default will be created)
  loadApplicationConfig();

  /// Try to connect to flash stored SSID, start AP if fails after timeout
  IPAddress myIP = myWebServer.startWiFi(15000, "ESP8266_AP", "123456789" );

  // Start WebSocket server on port 81
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Configure /setup page and start Web Server
  myWebServer.addOption(FILESYSTEM, "AP mode", apMode);
  myWebServer.addOption(FILESYSTEM, "LED Pin", ledPin);
  myWebServer.addOption(FILESYSTEM, "Option 1", option1.c_str());
  myWebServer.addOption(FILESYSTEM, "Option 2", option2);
  // Add custom page handlers
  myWebServer.webserver->on("/led", HTTP_GET, handleLed);

  if (myWebServer.begin()) {
    DebugPrint(F("ESP Web Server started on IP Address: "));
    DebugPrintln(myIP);
    DebugPrintln(F("Open /setup page to configure optional parameters"));
    DebugPrintln(F("Open /edit page to view and edit files"));
    DebugPrintln(F("Open /update page to upload firmware and filesystem updates"));
  }

  // Start MDSN responder
  if (WiFi.status() == WL_CONNECTED) {
    // Set hostname
#ifdef ESP8266
    WiFi.hostname(hostname);
    configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#elif defined(ESP32)
    WiFi.setHostname(hostname);
    configTzTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#endif
    if (MDNS.begin(hostname)) {
      DebugPrintln(F("MDNS responder started."));
      DebugPrintf("You should be able to connect with address\t http://%s.local/\n", hostname);
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80);
    }
  }

  pinMode(LED_BUILTIN, OUTPUT); 
}


void loop() {
  myWebServer.run();
  webSocket.loop();

  if(WiFi.status() == WL_CONNECTED) {
    #ifdef ESP8266
    MDNS.update();
    #endif
  }

  // Send ESP system time (epoch) to WS client
  static uint32_t sendToClientTime;
  if (millis() - sendToClientTime > 1000 ) {
    sendToClientTime = millis();
    time_t now = time(nullptr);
    char buffer[50];
    snprintf (buffer, sizeof(buffer), "{\"esptime\": %d}", (int)now);
    webSocket.broadcastTXT(buffer);
  }
}