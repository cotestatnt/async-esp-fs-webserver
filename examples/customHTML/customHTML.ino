#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>   // https://github.com/cotestatnt/async-esp-fs-webserver

const char* hostname = "myserver";
#define FILESYSTEM LittleFS
AsyncFsWebServer server(80, FILESYSTEM, hostname);
bool captiveRun = false;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#define CLEAR_OPTIONS false

// Test "options" values
uint8_t ledPin = LED_BUILTIN;
bool boolVar = true;
bool boolVar2 = false;
uint32_t longVar = 1234567890;
float floatVar = 15.5F;
String stringVar = "Test option String";

// ThingsBoard variables
String tb_deviceName = "ESP Sensor";
double tb_deviceLatitude = 41.88505;
double tb_deviceLongitude = 12.50050;
String tb_deviceToken = "xxxxxxxxxxxxxxxxxxx";
String tb_device_key = "xxxxxxxxxxxxxxxxxxx";
String tb_secret_key = "xxxxxxxxxxxxxxxxxxx";
String tb_serverIP = "thingsboard.cloud";
uint16_t tb_port = 80;

// Var labels (in /setup webpage)
#define LED_LABEL "The LED pin number"
#define BOOL_LABEL "A bool variable"
#define LONG_LABEL "A long variable"
#define FLOAT_LABEL "A float variable"
#define STRING_LABEL "A String variable"

#define TB_DEVICE_NAME "Device Name"
#define TB_DEVICE_LAT "Device Latitude"
#define TB_DEVICE_LON "Device Longitude"
#define TB_SERVER "ThingsBoard server address"
#define TB_PORT "ThingsBoard server port"
#define TB_DEVICE_TOKEN "ThingsBoard device token"
#define TB_DEVICE_KEY "Provisioning device key"
#define TB_SECRET_KEY "Provisioning secret key"

// Timezone definition to get properly time from NTP server
//n.u. #define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
//n.u. struct tm Time;

/*
* Include the custom HTML, CSS and Javascript to be injected in /setup webpage.
* HTML code will be injected according to the order of options declaration.
* CSS and JavaScript will be appended to the end of body in order to work properly.
* In this manner, is also possible override the default element styles
* like for example background color, margins, padding etc etc
*/
#include "customElements.h"
#include "thingsboard.h"

////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem() {
  if (FILESYSTEM.begin()){
    server.printFileList(FILESYSTEM, "/", 1);
    return true;
  }
  else {
    Serial.println("ERROR on mounting filesystem. It will be reformatted!");
    FILESYSTEM.format();
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


////////////////////  Load application options from filesystem  ////////////////////
/*
* Unlike what was done in customOptions.ino, in this example
* the variables are read (and written) all at once using the ArduinoJon library
*/
bool loadOptions() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    File file = server.getConfigFile("r");
#if ARDUINOJSON_VERSION_MAJOR > 6
    JsonDocument doc;
#else
    DynamicJsonDocument doc(file.size() * 1.33);
#endif
    if (!file)
      return false;

    DeserializationError error = deserializeJson(doc, file);
    if (error)
      return false;

    ledPin = doc[LED_LABEL];
    boolVar = doc[BOOL_LABEL];
    longVar = doc[LONG_LABEL];
    floatVar = doc[FLOAT_LABEL]["value"];
    stringVar = doc[STRING_LABEL].as<String>();

    tb_deviceName = doc[TB_DEVICE_NAME].as<String>();
    tb_deviceLatitude = doc[TB_DEVICE_LAT];
    tb_deviceLongitude = doc[TB_DEVICE_LON];
    tb_deviceToken = doc[TB_DEVICE_TOKEN].as<String>();
    tb_device_key = doc[TB_DEVICE_KEY].as<String>();
    tb_secret_key = doc[TB_SECRET_KEY].as<String>();
    tb_serverIP = doc[TB_SERVER].as<String>();
    tb_port = doc[TB_PORT];

    file.close();

    Serial.println();
    Serial.printf("LED pin value: %d\n", ledPin);
    Serial.printf("Bool value: %d\n", boolVar);
    Serial.printf("Long value: %ld\n",(long) longVar);
    Serial.printf("Float value: %d.%d\n", (int)floatVar, (int)(floatVar*1000)%1000);
    Serial.println(stringVar);
    return true;
  }
  else
    Serial.println(F("Configuration file not exist"));
  return false;
}


////////////////////////////  HTTP Request Handlers  ////////////////////////////////////
void handleLoadOptions(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Options loaded");
  loadOptions();
  Serial.println("Application option loaded after web request");
}

void setup() {
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  Serial.begin(115200);

  // Load configuration (if not present, default will be created when webserver will start)
#if CLEAR_OPTIONS
  if (server.clearOptions())
    ESP.restart();
#endif

  // FILESYSTEM INIT
  if (startFilesystem()){
    // Load configuration (if not present, default will be created when webserver will start)
    if (loadOptions())
      Serial.println(F("Application option loaded"));
    else
      Serial.println(F("Application options NOT loaded!"));
  }

  // Try to connect to stored SSID, start AP with captive portal if fails after timeout
  IPAddress myIP = server.startWiFi(15000);
  if (!myIP) {
    Serial.println("\n\nNo WiFi connection, start AP and Captive Portal\n");
    server.startCaptivePortal("ESP_AP", "123456789", "/setup");
    myIP = WiFi.softAPIP();
    captiveRun = true;
  }

  // Add custom page handlers to webserver
  server.on("/reload", HTTP_GET, handleLoadOptions);

  // Add a new options box
  server.addOptionBox("My Options");
  server.addOption(LED_LABEL, ledPin);
  server.addOption(LONG_LABEL, longVar);
  // Float fields can be configured with min, max and step properties
  server.addOption(FLOAT_LABEL, floatVar, 0.0, 100.0, 0.01);
  server.addOption(STRING_LABEL, stringVar);
  server.addOption(BOOL_LABEL, boolVar);
  server.addOption(BOOL_LABEL "2", boolVar2);
  const char* dropItem[3] = {"Item1", "Item2", "Item3"};
  server.addDropdownList("DROPDOWN_TEST", dropItem, 3);

  // Add a new options box with custom code injected
  server.addOptionBox("Custom HTML");
  // How many times you need (for example one in different option box)
  server.addHTML(custom_html, "fetch-test", /*overwrite*/ false);

  // Add a new options box
  server.addOptionBox("ThingsBoard");
  server.addOption(TB_DEVICE_NAME, tb_deviceName);
  server.addOption(TB_DEVICE_LAT, tb_deviceLatitude, -180.0, 180.0, 0.00001);
  server.addOption(TB_DEVICE_LON, tb_deviceLongitude, -180.0, 180.0, 0.00001);
  server.addOption(TB_SERVER, tb_serverIP);
  server.addOption(TB_PORT, tb_port);
  server.addOption(TB_DEVICE_KEY, tb_device_key);
  server.addOption(TB_SECRET_KEY, tb_secret_key);
  server.addOption(TB_DEVICE_TOKEN, tb_deviceToken);
  server.addHTML(thingsboard_htm, "ts", /*overwrite file*/ false);

  // CSS will be appended to HTML head
  server.addCSS(custom_css, "fetch", /*overwrite file*/ false);
  // Javascript will be appended to HTML body
  server.addJavascript(custom_script, "fetch", /*overwrite file*/ false);
  server.addJavascript(thingsboard_script, "ts", /*overwrite file*/ false);

  // Add custom page title to /setup
  server.setSetupPageTitle("Custom HTML Web Server");
  // Add custom logo to /setup page with custom size
  server.setLogoBase64(base64_logo, "128", "128", /*overwrite file*/ false);

  // Enable ACE FS file web editor and add FS info callback function
  server.enableFsCodeEditor();
  #ifdef ESP32
  server.setFsInfoCallback(getFsInfo);
  #endif

  // Start server
  if (server.init()) {
    Serial.print(F("\n\nWeb Server started on IP Address: "));
    Serial.println(myIP);
    Serial.println(F(
      "This is \"customHTML.ino\" example.\n"
      "Open /setup page to configure optional parameters.\n"
      "Open /edit page to view, edit or upload example or your custom webserver source files."
    ));
    Serial.printf("Ready! Open http://%s.local in your browser\n", hostname);
    if (captiveRun)
      Serial.print(F("Captive portal is running"));
  }

}


void loop() {
  if (captiveRun)
    server.updateDNS();
}
