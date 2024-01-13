#if defined(ESP8266)
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <ESPmDNS.h>
#endif
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>  // https://github.com/cotestatnt/async-esp-fs-webserver

#include "index_htm.h"

#define FILESYSTEM LittleFS

char const* hostname = "fsbrowser";
AsyncFsWebServer server(80, FILESYSTEM, hostname);

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
#define BOOT_BUTTON 0

// Log messages both on Serial and WebSocket clients
void wsLogPrintf(bool toSerial, const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, 128, format, args);
  va_end(args);
  server.wsBroadcast(buffer);
  if (toSerial)
    Serial.println(buffer);
}

// In this example a custom websocket event handler is used instead default
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      client->printf("{\"Websocket connected\": true, \"clients\": %u}", client->id());
      Serial.printf("Websocket client %u connected\n", client->id());
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("Websocket client %u connected\n", client->id());
      break;

    case WS_EVT_DATA:
      {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->opcode == WS_TEXT) {
          char msg[len+1];
          msg[len] = '\0';
          memcpy(msg, data, len);
          Serial.printf("Received message \"%s\"\n", msg);
        }
      }
      break;

    default:
      break;
  }
}

// Test "config" values
String option1 = "Test option String";
uint32_t option2 = 1234567890;
uint8_t ledPin = LED_BUILTIN;

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
struct tm Time;


////////////////////////////////  NTP Time  /////////////////////////////////////
void getUpdatedtime(const uint32_t timeout) {
  uint32_t start = millis();
  Serial.print("Sync time...");
  while (millis() - start < timeout && Time.tm_year <= (1970 - 1900)) {
    time_t now = time(nullptr);
    Time = *localtime(&now);
    delay(5);
  }
  Serial.println(" done.");
}


////////////////////////////////  Filesystem  /////////////////////////////////////////
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("\nListing directory: %s\n", dirname);
    File root = fs.open(dirname, "r");
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            if(levels){
            #ifdef ESP32
			  listDir(fs, file.path(), levels - 1);
			#elif defined(ESP8266)
			  listDir(fs, file.fullName(), levels - 1);
			#endif
            }
        } else {
            Serial.printf("|__ FILE: %s (%d bytes)\n",file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

bool startFilesystem() {
  if (FILESYSTEM.begin()) {
    listDir(LittleFS, "/", 1);
    return true;
  } else {
    Serial.println("ERROR on mounting filesystem. It will be reformatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
  return false;
}


////////////////////  Load and save application configuration from filesystem  ////////////////////
void saveApplicationConfig() {
  File file = server.getConfigFile("r");
  DynamicJsonDocument doc(file.size() * 1.33);
  doc["Option 1"] = option1;
  doc["Option 2"] = option2;
  doc["LED Pin"] = ledPin;
  serializeJsonPretty(doc, file);
  file.close();
  delay(1000);
  ESP.restart();
}

bool loadApplicationConfig() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    File file = server.getConfigFile("r");
    DynamicJsonDocument doc(file.size() * 1.33);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error) {
      option1 = doc["Option 1"].as<String>();
      option2 = doc["Option 2"];
      ledPin = doc["LED Pin"];
      return true;
    } else {
      Serial.print(F("Failed to deserialize JSON. Error: "));
      Serial.println(error.c_str());
    }
  }
  return false;
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  delay(1000);

  // FILESYSTEM INIT
  if (startFilesystem()) {
    // Load configuration (if not present, default will be created when webserver will start)
    if (loadApplicationConfig())
      Serial.println(F("Application option loaded"));
    else
      Serial.println(F("Application options NOT loaded!"));
  }

  // Try to connect to stored SSID, start AP if fails after timeout
  IPAddress myIP = server.startWiFi(15000, "ESP_AP", "123456789");

  // Configure /setup page
  server.addOptionBox("My Options");
  server.addOption("LED Pin", ledPin);
  server.addOption("Option 1", option1.c_str());
  server.addOption("Option 2", option2);

  // Add custom page handlers
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", homepage);
  });

  // Enable ACE FS file web editor and add FS info callback function
  server.enableFsCodeEditor();
  /*
  * Getting FS info (total and free bytes) is strictly related to
  * filesystem library used (LittleFS, FFat, SPIFFS etc etc)
  * (On ESP8266 will be used "built-in" fsInfo data type)
  */
#ifdef ESP32
  server.setFsInfoCallback([](fsInfo_t* fsInfo) {
	fsInfo->fsName = "LittleFS";
	fsInfo->totalBytes = LittleFS.totalBytes();
	fsInfo->usedBytes = LittleFS.usedBytes();  
  });
#endif

  // Init with custom WebSocket event handler and start server
  server.init(onWsEvent);

  Serial.print(F("ESP Web Server started on IP Address: "));
  Serial.println(WiFi.localIP());
  Serial.println(F(
    "This is \"withWebSocket.ino\" example.\n"
    "Open /setup page to configure optional parameters.\n"
    "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));

  // Set hostname
#ifdef ESP8266
  WiFi.hostname(hostname);
  configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#elif defined(ESP32)
  WiFi.setHostname(hostname);
  configTzTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#endif

  // Start MDSN responder
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(hostname)) {
      Serial.println(F("MDNS responder started."));
      Serial.printf("You should be able to connect with address  http://%s.local/\n", hostname);
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80);
    }
  }
}


void loop() {

  if (digitalRead(BOOT_BUTTON) == LOW) {
    wsLogPrintf(true, "Button on GPIO %d clicked", BOOT_BUTTON);
    delay(1000);
  }

  if (WiFi.status() == WL_CONNECTED) {
#ifdef ESP8266
    MDNS.update();
#endif
  }

  // Send ESP system time (epoch) to WS client
  static uint32_t sendToClientTime;
  if (millis() - sendToClientTime > 1000) {
    sendToClientTime = millis();
    time_t now = time(nullptr);
    wsLogPrintf(false, "{\"esptime\": %d}", (int)now);
  }
}