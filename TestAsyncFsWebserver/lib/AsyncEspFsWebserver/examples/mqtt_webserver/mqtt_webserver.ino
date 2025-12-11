/*
 Basic ESP32 MQTT example:
 It connects to an MQTT server then:
  - publishes "hello world" to the topic "output" every two seconds
  - subscribes to the topic "input", printing out any (string) messages it receives.
  - If the first character of the topic "input" is an 1, switch ON the ESP Led, else switch it off
  It will reconnect to the server using a NON blocking reconnect function. 
*/

#include <WiFi.h>
#include <PubSubClient.h>      // https://github.com/knolleary/pubsubclient/
#include <AsyncFsWebServer.h>  // https://github.com/cotestatnt/esp-fs-webserver

#include <FS.h>
#include <LittleFS.h>

// Define built-in LED if not defined by board (eg. generic dev boards)
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

AsyncFsWebServer myWebServer(80, LittleFS, "myServer");

// Update these with values suitable for your network.
const char* mqtt_server = "broker.mqtt-dashboard.com";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Let'use our unique clientID and topics (tied to cliendId in setup())
char clientId[16];
char inTopic[24];
char outTopic[24];

////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem() {
  if (LittleFS.begin()) {
    File root = LittleFS.open("/", "r");
    File file = root.openNextFile();
    while (file) {
      Serial.printf("FS File: %s, size: %d\n", file.name(), file.size());
      file = root.openNextFile();
    }
    return true;
  } else {
    Serial.println("ERROR on mounting filesystem. It will be reformatted!");
    LittleFS.format();
    ESP.restart();
  }
  return false;
}

///////////////////////////  MQTT callback function  ///////////////////////////////////
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message arrived [%s] ", topic);
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character (LED on with LOW signal on most boards)
  digitalWrite(LED_BUILTIN, (char)payload[0] == '1' ? LOW : HIGH);
}

///////////////////////////  MQTT reconnect function  ///////////////////////////////////
void mqttReconnect() {

  if (WiFi.status() != WL_CONNECTED)  // Check connection
    return;

  static uint32_t lastConnectionTime = 5000;
  if (millis() - lastConnectionTime < 5000)  // Wait 5 seconds before retrying
    return;
  lastConnectionTime = millis();

  Serial.print("Attempting MQTT connection...");
  if (mqttClient.connect(clientId)) {  // Attempt to connect
    Serial.println("connected");

    String payload = "Hello World from ";
    payload += clientId;
    mqttClient.publish(outTopic, payload.c_str());  // Once connected, publish an announcement...
    mqttClient.subscribe(inTopic);                  // ... and resubscribe
  } else {
    Serial.printf("failed, rc=%d, try again in 5 seconds\n", mqttClient.state());
  }
}



void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // Initialize the LED_BUILTIN pin as an output
  Serial.begin(115200);

  // LittleFS filesystem init
  startFilesystem();

  // Try to connect to flash stored SSID, start AP if fails after timeout
  if (!myWebServer.startWiFi(10000)) {
    Serial.println("\nWiFi not connected! Starting AP mode...");
    myWebServer.startCaptivePortal("ESP_AP", "123456789", "/setup");
  }

  // Enable ACE FS file web editor and add FS info callback function
  myWebServer.enableFsCodeEditor();
  myWebServer.setFsInfoCallback([](fsInfo_t* fsInfo) {
    fsInfo->fsName = "LittleFS";
    fsInfo->totalBytes = LittleFS.totalBytes();
    fsInfo->usedBytes = LittleFS.usedBytes();
  });

  // Start webserver
  myWebServer.init();
  Serial.print("Async ESP Web Server started on IP Address: ");
  Serial.println(myWebServer.getServerIP());
  Serial.println(
    "This is \"simpleServer.ino\" example.\n"
    "Open /setup page to configure optional parameters.\n"
    "Open /edit page to view, edit or upload example or your custom webserver source files."
  );

  

  // Create a unique mqttClient ID and in/out topics
  snprintf(clientId, sizeof(clientId), "ESP-%llX", ESP.getEfuseMac());
  snprintf(inTopic, sizeof(inTopic), "%s/input", clientId);
  snprintf(outTopic, sizeof(outTopic), "%s/output", clientId);

  Serial.print("MQTT CLiend ID: ");
  Serial.println(clientId);
  Serial.print("Publish output topic: ");
  Serial.println(outTopic);
  Serial.print("Subscribe input topic: ");
  Serial.println(inTopic);

  // Set MQTT server and callback function
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
}

void loop() {

  // Handle MQTT client
  if (!mqttClient.connected()) {
    // Client not connected, try to reconnect every 5 seconds
    mqttReconnect();
  } else {
    // Client connected
    mqttClient.loop();
    // Publish a new message every 5 seconds
    static uint32_t lastMsgTime = millis();
    static uint16_t value = 0;
    if (millis() - lastMsgTime > 5000) {
      lastMsgTime = millis();

      char payload[64];
      snprintf(payload, sizeof(payload), "Hello World from %s #%d", clientId, ++value);

      Serial.print("Publish message: ");
      Serial.println(payload);
      mqttClient.publish(outTopic, payload);
    }
  }

  delay(1);
}
