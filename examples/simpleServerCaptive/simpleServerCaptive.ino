#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

AsyncFsWebServer server(80, LittleFS, "myServer");
bool captiveRun = false;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
const int ledPin = LED_BUILTIN;

////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem() {
  if (LittleFS.begin()){
    server.printFileList(LittleFS, "/", 1);
    return true;
  }
  else {
    Serial.println("ERROR on mounting filesystem. It will be formatted!");
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
  Serial.print("handleLed:");
  Serial.println(reply);
}


void setup() {
    pinMode(ledPin, OUTPUT);
    Serial.begin(115200);
    delay(1000);

    // Init and start LittleFS file system
    startFilesystem();
	
	// Try to connect to WiFi (will start AP if not connected after timeout)
    if (!server.startWiFi(10000)) {
        Serial.println("\nWiFi not connected! Starting AP mode...");
        server.startCaptivePortal("ESP_AP", "123456789", "/setup");
        captiveRun = true;
    }
   
    // Set a custom /setup page title
    server.setSetupPageTitle("Simple Async FS Captive Web Server");

    // Enable ACE FS file web editor and add FS info callback function
    server.enableFsCodeEditor();
    #ifdef ESP32
    server.setFsInfoCallback(getFsInfo);
    #endif

    // Add 0 callback function handler
    server.on("/led", HTTP_GET, handleLed);

    // Start server
    server.init();
    Serial.print(F("Async ESP Web Server started on IP Address: "));
    Serial.println(server.getServerIP());
    Serial.println(F(
        "This is \"simpleServerCaptive.ino\" example.\n"
        "Open /setup page to configure optional parameters.\n"
        "Open /edit page to view, edit or upload example or your custom webserver source files."
    ));

}

void loop() {
    if (captiveRun)
        server.updateDNS();
    
    // This delay is required in order to avoid loopTask() WDT reset on ESP32
    delay(1);  
}
