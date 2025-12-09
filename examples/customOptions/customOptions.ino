#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>  //https://github.com/cotestatnt/async-esp-fs-webserver

#define FILESYSTEM LittleFS

// AsyncFsWebServer server(80, FILESYSTEM, "esphost");
AsyncFsWebServer server(80, FILESYSTEM);

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
#define BOOT_PIN    0

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
      openModal('Options loaded', 'Options was reloaded from configuration file');
      return;
    }
    throw new Error('Something goes wrong with fetch');
  })
  .catch((error) => {
    openModal('Error', 'Something goes wrong with your request');
  });
}
)EOF";


// Callback: notify user when the configuration file is saved
void onConfigSaved(const char* path) {
  Serial.printf("\n[Config] File salvato: %s\n", path);
}

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


////////////////////  Load application options from filesystem  ////////////////////
bool loadOptions() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    server.getOptionValue(LED_LABEL, ledPin);
    server.getOptionValue(BOOL_LABEL, boolVar);
    server.getOptionValue(LONG_LABEL, longVar);
    server.getOptionValue(FLOAT_LABEL, floatVar);
    server.getOptionValue(STRING_LABEL, stringVar);
    server.getOptionValue(DROPDOWN_LABEL, dropdownSelected);
    server.closeSetupConfiguration();  // Close configuration to free resources

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


////////////////////////////  HTTP Request Handlers  ////////////////////////////////////
void handleLoadOptions(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Options loaded");
  loadOptions();
  Serial.println("Application option loaded after web request");
}


void setup() {
  Serial.begin(115200);

  // FILESYSTEM INIT
  if (startFilesystem()){
    // Load configuration (if not present, default will be created when webserver will start)
    loadOptions();
  }

  // Try to connect to WiFi (will start AP if not connected after timeout)
  if (!server.startWiFi(10000)) {
    Serial.println("\nWiFi not connected! Starting AP mode...");
    server.startCaptivePortal("ESP_AP", "123456789", "/setup");
  }

  // Add custom page handler
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
#ifdef ESP32
  server.enableFsCodeEditor([](fsInfo_t* fsInfo) {
    fsInfo->fsName = "LittleFS";
    fsInfo->totalBytes = LittleFS.totalBytes();
    fsInfo->usedBytes = LittleFS.usedBytes();
  });
#else
  // ESP8266 core support LittleFS by default
  server.enableFsCodeEditor();
#endif

  // set /setup and /edit page authentication
  server.setAuthentication("admin", "admin");

  // Inform user when config.json is saved via /edit or /upload
  server.setConfigSavedCallback(onConfigSaved);

  // Start server
  server.init();
  Serial.print(F("\nESP Web Server started on IP Address: "));
  Serial.println(server.getServerIP());
  Serial.println(F(
      "\nThis is \"customOptions.ino\" example.\n"
      "Open /setup page to configure optional parameters.\n"
      "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));
}

void loop() {
  if (server.isAccessPointMode())
    server.updateDNS();

  // Keep BOOT_PIN pressed 5 seconds to clear application options
  static unsigned long buttonPressStart = 0;
  static bool buttonPressed = false;
  
  if (digitalRead(BOOT_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
    } 
    else if (millis() - buttonPressStart >= 5000) {
      Serial.println("\nClearing application options...");
      server.clearConfigFile();
      delay(1000);
      ESP.restart();
    }
  } else {
    buttonPressed = false;
  }

  // Nothing to do here, just a small delay for task yield
  delay(10);  
}
