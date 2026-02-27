
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>   // https://github.com/cotestatnt/async-esp-fs-webserver/

#define FILESYSTEM LittleFS
AsyncFsWebServer server(FILESYSTEM, 80);

unsigned long lastGpioBroadcastMs = 0;
const unsigned long GPIO_BROADCAST_INTERVAL_MS = 200; 

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif


// Define a struct for store all info about each gpio
struct gpio_type {
  uint8_t type;
  uint8_t pin;
  bool level;  
  const char* label;  
};

// Define an array of struct GPIO and initialize with values

/* ESP32 - NodeMCU-32S */
gpio_type gpios[] = {
  {INPUT_PULLUP, 0, false, "GPIO 0"},  
  {INPUT_PULLUP, 2, false, "GPIO 2"},
  {INPUT_PULLUP, 12, false, "GPIO 12"},
  {OUTPUT, 4, false, "GPIO 4"},
  {OUTPUT, 5, false, "GPIO 5"},
  {OUTPUT, LED_BUILTIN, false, "LED BUILTIN"}
};


////////////////////////////////   WebSocket Handler  /////////////////////////////
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  switch (type) {
    case WS_EVT_DISCONNECT:
      Serial.print("WebSocket client disconnected!\n");
      break;
    case WS_EVT_CONNECT:  {
        IPAddress ip = client->remoteIP();
        Serial.printf("WebSocket client %d.%d.%d.%d connected.\n", ip[0], ip[1], ip[2], ip[3]);
        client->printf("%s", "{\"Connected\": true}");
      }
      break;
    case WS_EVT_DATA: {
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        String msg = "";
        if(info->final && info->index == 0 && info->len == len){
          //the whole message is in a single frame and we got all of it's data
          if (info->opcode == WS_TEXT){
            for(size_t i=0; i < info->len; i++) {
              msg += (char) data[i];
            }
          }
          Serial.printf("WS message: %s\n",msg.c_str());
          if (msg[0] == '{')
            parseMessage(msg);
        }
      }
    default:
      break;
  }
}


void parseMessage(const String json) {
  using namespace AsyncFSWebServer;
  CJSON::Json doc;
  
  if (doc.parse(json)) {
    // If this is a "writeOut" command, set the pin level to value
    String cmd;
    if (doc.getString("cmd", cmd)) {
      if (cmd == "writeOut") {
        double pin_val, level_val;
        if (doc.getNumber("pin", pin_val) && doc.getNumber("level", level_val)) {
          int pin = (int)pin_val;
          int level = (int)level_val;
          for (gpio_type &gpio : gpios) {
            if (gpio.pin == pin) {
              Serial.printf("Set pin %d to %d\n", pin, level);
              gpio.level = level;
              digitalWrite(pin, level);
              updateGpioList(nullptr);
              return;
            }
          }
        }
      }
    }
  } else {
    Serial.println(F("Failed to parse JSON message"));
  }
}

void updateGpioList(AsyncWebServerRequest *request) {
  using namespace AsyncFSWebServer;
  CJSON::Json doc;

  int index = 0;
  for (gpio_type &gpio : gpios) {
    String key = String(index++);
    doc.ensureObject(key);
    doc.setNumber(key, "type", gpio.type);
    doc.setNumber(key, "pin", gpio.pin);
    doc.setString(key, "label", String(gpio.label));
    doc.setBool(key, "level", gpio.level);
  }

  String json = doc.serialize();

  // Update client via websocket
  server.wsBroadcast(json.c_str());

  if (request != nullptr)
    request->send(200, "application/json", json);
}

bool updateGpioState() {
  // Iterate the array of GPIO struct and check level of inputs
  for (gpio_type &gpio : gpios) {
    if (gpio.type != OUTPUT) {
      // Input value != from last read
      if (digitalRead(gpio.pin) != gpio.level) {
        gpio.level = digitalRead(gpio.pin);
        return true;
      }
    }
  }
  return false;
}



void setup() {
  Serial.begin(115200);

  // FILESYSTEM initialization
  if (!FILESYSTEM.begin()) {
    Serial.println("ERROR on mounting filesystem.");
    //FILESYSTEM.format();
    ESP.restart();
  }
  
   
  // Try to connect to WiFi (will start AP if not connected after timeout)
  if (!server.startWiFi(10000)) {
    Serial.println("\nWiFi not connected! Starting AP mode...");
    server.startCaptivePortal("ESP_AP", "123456789", "/setup");
  }

  // Enable ACE FS file web editor and add FS info callback function
  server.enableFsCodeEditor();

  // Add custom page handlers
  server.on("/getGpioList", HTTP_GET, updateGpioList);

  // Start server with custom websocket event handler
  server.init(onWsEvent);
  Serial.print(F("ESP Web Server started on IP Address: "));
  Serial.println(server.getServerIP());
  Serial.println(F(
    "This is \"gpio_list.ino\" example.\n"
    "Open /setup page to configure optional parameters.\n"
    "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));

  // GPIOs configuration
  for (gpio_type &gpio : gpios) {    
    pinMode(gpio.pin, gpio.type);    
    digitalWrite(gpio.pin, gpio.level);
  }
}

void loop() {
  updateGpioState();

  // True on pin state change
  if (updateGpioState()) {    
    if ( millis() - lastGpioBroadcastMs >= GPIO_BROADCAST_INTERVAL_MS) {
      lastGpioBroadcastMs =  millis();
      updateGpioList(nullptr);   // Push new state to web clients via websocket
    }
  }

  // Small delay to avoid busy loop
  delay(10);
}