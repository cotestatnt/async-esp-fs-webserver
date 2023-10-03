

#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

#define FILESYSTEM LittleFS

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
// we need a list ef values and a variable to store the selected option
#define LIST_SIZE  7
const char* dropdownList[LIST_SIZE] = {
  "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
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
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("\nListing directory: %s\n", dirname);
    File root = fs.open(dirname);
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
              listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.printf("|__ FILE: %s (%d bytes)\n",file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

bool startFilesystem() {
  if (FILESYSTEM.begin()){
    listDir(FILESYSTEM, "/", 1);
    return true;
  }
  else {
      Serial.println("ERROR on mounting filesystem. It will be formmatted!");
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
    Serial.printf("Long value: %lu\n",longVar);
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

  // Try to connect to stored SSID, start AP if fails after timeout
  IPAddress myIP = server.startWiFi(15000, "ESP8266_AP", "123456789" );

  // FILESYSTEM INIT
  if (startFilesystem()){
    // Load configuration (if not present, default will be created when webserver will start)
    if (loadOptions())
      Serial.println(F("Application option loaded"));
    else
      Serial.println(F("Application options NOT loaded!"));
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

  server.addHTML(save_btn_htm, "buttons");
  server.addJavascript(button_script);
  
  // Enable ACE FS file web editor and add FS info callback fucntion
  server.enableFsCodeEditor();
  #ifdef ESP32
  server.setFsInfoCallback(getFsInfo);
  #endif

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
  // Savew options also on button click
  if (! digitalRead(BTN_SAVE)) {
    saveOptions();
    delay(1000);
  }
}
