#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>   // https://github.com/cotestatnt/async-esp-fs-webserver/
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#define FILESYSTEM LittleFS

AsyncFsWebServer server(FILESYSTEM, 80, "mywebserver");

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
struct tm Time;

////////////////////////////////   WebSocket Handler  /////////////////////////////
void webSocketEvent(AsyncWebSocket *serverPtr, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT: {
        IPAddress ip = client->remoteIP();
        Serial.printf("Hello client #%u [%s]\n", client->id(), ip.toString().c_str());
      }
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[%u] Disconnected!\n", client->id());
      break;
    case WS_EVT_DATA:
      Serial.printf("[%u] get %u bytes\n", client->id(), (unsigned)len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
    default:
      break;
  }
}

////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem() {
  if (FILESYSTEM.begin()){
    server.printFileList(FILESYSTEM, "/", 1, Serial);
    return true;
  }
  else {
    Serial.println("ERROR on mounting filesystem. It will be reformatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
  return false;
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  // FILESYSTEM INIT
  startFilesystem();

  // Try to connect to WiFi (will start AP if not connected after timeout)
  if (!server.startWiFi(10000)) {
    Serial.println("\nWiFi not connected! Starting AP mode...");
    server.startCaptivePortal("ESP_AP", "123456789", "/setup");
  }

  // Enable ACE FS file web editor
  server.enableFsCodeEditor();

  // Enable websocket at /ws with our handler
  server.enableWebSocket("/ws", webSocketEvent);

  // Start HTTP server
  server.init();

  Serial.print(F("ESP Async Web Server started on IP Address: "));
  Serial.println(server.getServerIP());
  Serial.println(F(
    "This is \"websocketEcharts.ino\" example (AsyncFsWebServer).\n"
    "Open /setup page to configure optional parameters.\n"
    "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));
}


void loop() {
  // Send ESP system time (epoch) and heap stats to WS clients
  static uint32_t sendToClientTime;
  if (millis() - sendToClientTime > 1000 ) {
    sendToClientTime = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    time_t now = time(nullptr);
    CJSON::Json doc;
    doc.setBool("addPoint", true);
    doc.setNumber("timestamp", (double)now);
#ifdef ESP32
    doc.setNumber("totalHeap", (double)heap_caps_get_free_size(0));
    doc.setNumber("maxBlock", (double)heap_caps_get_largest_free_block(0));
#elif defined(ESP8266)
    uint32_t free;
    uint32_t max;
    ESP.getHeapStats(&free, &max, nullptr);
    doc.setNumber("totalHeap", (double)free);
    doc.setNumber("maxBlock", (double)max);
#endif
    doc.setNumber("rssi", (double)WiFi.RSSI());
    String msg = doc.serialize();
    server.wsBroadcast(msg.c_str());
  }
}
