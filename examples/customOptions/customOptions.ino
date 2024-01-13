

#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>  //https://github.com/cotestatnt/async-esp-fs-webserver

#define FILESYSTEM LittleFS
bool captiveRun = false;

// AsyncFsWebServer server(80, FILESYSTEM, "esphost");
AsyncFsWebServer server(80, FILESYSTEM);

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#define BTN_SAVE  5

// Test "options" values
uint8_t ledPin = LED_BUILTIN;
bool boolVar = true;
uint32_t longVar = 1234567890;
float floatVar = 15.5F;
String stringVar = "Test option String";

// In order to show a dropdown list box in /setup page
// we need a list of values and a variable to store the selected option
#define LIST_SIZE  7
const char* dropdownList[LIST_SIZE] =
{"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
String dropdownSelected;

// Var labels (in /setup webpage)
#define LED_LABEL "The LED pin number"
#define BOOL_LABEL "A bool variable"
#define LONG_LABEL "A long variable"
#define FLOAT_LABEL "A float variable"
#define STRING_LABEL "A String variable"
#define DROPDOWN_LABEL "A dropdown listbox"

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
struct tm Time;

static const char save_btn_htm[] PROGMEM = R"EOF(
<div class="btn-bar">
  <a class="btn" id="reload-btn">Reload options</a>
</div>
)EOF";

static const char button_script[] PROGMEM = R"EOF(
/* Add click listener to button */
document.getElementById('reload-btn').addEventListener('click', reload);
function reload() {
  console.log('Reload configuration options');
  fetch('/reload')
  .then((response) => {
    if (response.ok) {
      openModalMessage('Options loaded', 'Options was reloaded from configuration file');
      return;
    }
    throw new Error('Something goes wrong with fetch');
  })
  .catch((error) => {
    openModalMessage('Error', 'Something goes wrong with your request');
  });
}
)EOF";


////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem() {
  if (FILESYSTEM.begin()){
    server.printFileList(FILESYSTEM, "/", 2);
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
bool loadOptions() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    server.getOptionValue(LED_LABEL, ledPin);
    server.getOptionValue(BOOL_LABEL, boolVar);
    server.getOptionValue(LONG_LABEL, longVar);
    server.getOptionValue(FLOAT_LABEL, floatVar);
    server.getOptionValue(STRING_LABEL, stringVar);
    server.getOptionValue(DROPDOWN_LABEL, dropdownSelected);

    Serial.println("\nThis are the current values stored: \n");
    Serial.printf("LED pin value: %d\n", ledPin);
    Serial.printf("Bool value: %s\n", boolVar ? "true" : "false");
    Serial.printf("Long value: %u\n", longVar);
    Serial.printf("Float value: %d.%d\n", (int)floatVar, (int)(floatVar*1000)%1000);
    Serial.printf("String value: %s\n", stringVar.c_str());
    Serial.printf("Dropdown selected value: %s\n\n", dropdownSelected.c_str());
    return true;
  }
  else
    Serial.println(F("Config file not exist"));
  return false;
}

void saveOptions() {
  // server.saveOptionValue(LED_LABEL, ledPin);
  // server.saveOptionValue(BOOL_LABEL, boolVar);
  // server.saveOptionValue(LONG_LABEL, longVar);
  // server.saveOptionValue(FLOAT_LABEL, floatVar);
  // server.saveOptionValue(STRING_LABEL, stringVar);
  // server.saveOptionValue(DROPDOWN_LABEL, dropdownSelected);
  Serial.println(F("Application options saved."));
}

////////////////////////////  HTTP Request Handlers  ////////////////////////////////////
void handleLoadOptions(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Options loaded");
  loadOptions();
  Serial.println("Application option loaded after web request");
}


void setup() {
  Serial.begin(115200);
  pinMode(BTN_SAVE, INPUT_PULLUP);

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

  // Configure /setup page and start Web Server
  server.addOptionBox("My Options");

  server.addOption(BOOL_LABEL, boolVar);
  server.addOption(LED_LABEL, ledPin);
  server.addOption(LONG_LABEL, longVar);
  server.addOption(FLOAT_LABEL, floatVar, 1.0, 100.0, 0.01);
  server.addOption(STRING_LABEL, stringVar);
  server.addDropdownList(DROPDOWN_LABEL, dropdownList, LIST_SIZE);

  server.addHTML(save_btn_htm, "buttons", /*overwrite*/ false);
  server.addJavascript(button_script, "js", /*overwrite*/ false);

  // Enable ACE FS file web editor and add FS info callback function
  server.enableFsCodeEditor();
  #ifdef ESP32
  server.setFsInfoCallback(getFsInfo);
  #endif

  // set /setup and /edit page authentication
  server.setAuthentication("admin", "admin");

  // Start server
  server.init();
  Serial.print(F("ESP Web Server started on IP Address: "));
  Serial.println(myIP);
  Serial.println(F(
      "This is \"customOptions.ino\" example.\n"
      "Open /setup page to configure optional parameters.\n"
      "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));
}

void loop() {
  if (captiveRun)
    server.updateDNS();

  // Savew options also on button click
  if (! digitalRead(BTN_SAVE)) {
    saveOptions();
    delay(1000);
  }
}
