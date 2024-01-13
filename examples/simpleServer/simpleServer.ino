#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

AsyncFsWebServer server(80, LittleFS, "myServer");
int testInt = 150;
float testFloat = 123.456;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
const uint8_t ledPin = LED_BUILTIN;


// FILESYSTEM INIT
bool startFilesystem(){
  if (LittleFS.begin()){
      File root = LittleFS.open("/", "r");
      File file = root.openNextFile();
      while (file){
          Serial.printf("FS File: %s, size: %d\n", file.name(), file.size());
          file = root.openNextFile();
      }
      return true;
  }
  else {
      Serial.println("ERROR on mounting filesystem. It will be reformatted!");
      LittleFS.format();
      ESP.restart();
  }
  return false;
}


/*
* Getting FS info (total and free bytes) is strictly related to
* filesystem library used (LittleFS, FFat, SPIFFS etc etc) and ESP framework
*/
#ifdef ESP32
void getFsInfo(fsInfo_t* fsInfo) {
	fsInfo->fsName = "LittleFS";
	fsInfo->totalBytes = LittleFS.totalBytes();
	fsInfo->usedBytes = LittleFS.usedBytes();  
}
#endif


//---------------------------------------
void handleLed(AsyncWebServerRequest *request) {
  static int value = false;
  // http://xxx.xxx.xxx.xxx/led?val=1
  if(request->hasParam("val")) {
    value = request->arg("val").toInt();
    digitalWrite(ledPin, value);
  }
  String reply = "LED is now ";
  reply += value ? "ON" : "OFF";
  request->send(200, "text/plain", reply);
}


void setup() {
    pinMode(ledPin, OUTPUT);
    Serial.begin(115200);
    delay(1000);
    if (startFilesystem()) {
        Serial.println("LittleFS filesystem ready!");
        File config = server.getConfigFile("r");
        if (config) {
            DynamicJsonDocument doc(config.size() * 1.33);
            deserializeJson(doc, config);
            testInt = doc["Test int variable"];
            testFloat = doc["Test float variable"];
        }
        Serial.printf("Stored \"testInt\" value: %d\n", testInt);
        Serial.printf("Stored \"testFloat\" value: %3.2f\n", testFloat);
    }
    else
        Serial.println("LittleFS error!");

    IPAddress myIP = server.startWiFi(15000, "ESP32_AP1234", "123456789");
    WiFi.setSleep(WIFI_PS_NONE);
    server.addOptionBox("Custom options");
    server.addOption("Test int variable", testInt);
    server.addOption("Test float variable", testFloat);
    server.setSetupPageTitle("Simple Async ESP FS WebServer");

    // Enable ACE FS file web editor and add FS info callback function
    server.enableFsCodeEditor();
    #ifdef ESP32
    server.setFsInfoCallback(getFsInfo);
    #endif

    server.on("/led", HTTP_GET, handleLed);

    // Start server
    server.init();
    Serial.print(F("Async ESP Web Server started on IP Address: "));
    Serial.println(myIP);
    Serial.println(F(
        "This is \"simpleServer.ino\" example.\n"
        "Open /setup page to configure optional parameters.\n"
        "Open /edit page to view, edit or upload example or your custom webserver source files."
    ));

}

void loop() {

}
