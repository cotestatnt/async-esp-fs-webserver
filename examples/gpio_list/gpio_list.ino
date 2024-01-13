#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>   // https://github.com/cotestatnt/async-esp-fs-webserver/

#define FILESYSTEM LittleFS
AsyncFsWebServer server(80, FILESYSTEM);

// Define a struct for store all info about each gpio
struct gpio_type {
  const char* type;
  const char* label;
  int pin;
  bool level;
};

// Define an array of struct GPIO and initialize with values

/* (ESP32-C3) */
/*
gpio_type gpios[NUM_GPIOS] = {
  {"input", "INPUT 2", 2},
  {"input", "INPUT 4", 4},
  {"input", "INPUT 5", 5},
  {"output", "OUTPUT 6", 6},
  {"output", "OUTPUT 7", 7},
  {"output", "LED BUILTIN", 3} // Led ON with signal HIGH
};
*/

/* ESP8266 - Wemos D1-mini */
/*
  gpio_type gpios[] = {
  {"input", "INPUT 5", D5},
  {"input", "INPUT 6", D6},
  {"input", "INPUT 7", D7},
  {"output", "OUTPUT 2", D2},
  {"output", "OUTPUT 3", D3},
  {"output", "LED BUILTIN", LED_BUILTIN} // Led ON with signal LOW usually
  };
*/

/* ESP32 - NodeMCU-32S */
gpio_type gpios[] = {
  {"input", "INPUT 18", 18},
  {"input", "INPUT 19", 19},
  {"input", "INPUT 21", 21},
  {"output", "OUTPUT 4", 4},
  {"output", "OUTPUT 5", 5},
  {"output", "LED BUILTIN", 2} // Led ON with signal HIGH
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
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, json);

  if (!error) {
    // If this is a "writeOut" command, set the pin level to value
    const char* cmd = doc["cmd"];
    if (strcmp(cmd, "writeOut") == 0) {
      int pin = doc["pin"];
      int level = doc["level"];
      for (gpio_type &gpio : gpios) {
        if (gpio.pin == pin) {
          Serial.printf("Set pin %d to %d\n", pin, level);
          gpio.level = level ;
          digitalWrite(pin, level);
          updateGpioList(nullptr);
          return;
        }
      }
    }
  }
  Serial.print(F("deserializeJson() failed: "));
  Serial.println(error.f_str());
}

void updateGpioList(AsyncWebServerRequest *request) {
  StaticJsonDocument<512> doc;
  JsonArray array = doc.to<JsonArray>();

  // Create a JSON message with current GPIO state
  for (gpio_type &gpio : gpios) {
    JsonObject obj = array.createNestedObject();
    obj["type"] = gpio.type;
    obj["pin"] = gpio.pin;
    obj["label"] = gpio.label;
    obj["level"] = gpio.level;
  }
  // Update client via websocket
  String json;
  serializeJson(doc, json);
  server.wsBroadcast(json.c_str());

  if (request != nullptr)
    request->send(200, "text/plain", json);
}

bool updateGpioState() {
  // Iterate the array of GPIO struct and check level of inputs
  for (gpio_type &gpio : gpios) {
    if (strcmp(gpio.type, "input") == 0) {
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

  // Try to connect to flash stored SSID, start AP if fails after timeout
  IPAddress myIP = server.startWiFi(15000, "ESP_AP", "123456789" );

  // Enable ACE FS file web editor and add FS info callback function
  server.enableFsCodeEditor();

  /*
  * Getting FS info (total and free bytes) is strictly related to
  * filesystem library used (LittleFS, FFat, SPIFFS etc etc) and ESP framework
  */
  #ifdef ESP32
  server.setFsInfoCallback([](fsInfo_t* fsInfo) {
	  fsInfo->fsName = "LittleFS";
	  fsInfo->totalBytes = LittleFS.totalBytes();
	  fsInfo->usedBytes = LittleFS.usedBytes();  
  });
  #endif

  // Add custom page handlers
  server.on("/getGpioList", HTTP_GET, updateGpioList);

  // Start server with custom websocket event handler
  server.init(onWsEvent);
  Serial.print(F("ESP Web Server started on IP Address: "));
  Serial.println(myIP);
  Serial.println(F(
    "This is \"gpio_list.ino\" example.\n"
    "Open /setup page to configure optional parameters.\n"
    "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));

  // GPIOs configuration
  for (gpio_type &gpio : gpios) {
    if (strcmp(gpio.type, "input") == 0)
        pinMode(gpio.pin, INPUT_PULLUP);
    else
      pinMode(gpio.pin, OUTPUT);
  }
}

void loop() {

  // True on pin state change
  if (updateGpioState()) {
    updateGpioList(nullptr);   // Push new state to web clients via websocket
  }
}