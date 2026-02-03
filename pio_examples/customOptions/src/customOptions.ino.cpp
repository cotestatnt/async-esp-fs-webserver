# 1 "C:\\Users\\TOLENT~1\\AppData\\Local\\Temp\\tmpqi3ucfn5"
#include <Arduino.h>
# 1 "C:/Users/Tolentino/Documents/Arduino/libraries/async-esp-fs-webserver/pio_examples/customOptions/src/customOptions.ino"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>


#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
struct tm Time;

#define FILESYSTEM LittleFS
AsyncFsWebServer server(FILESYSTEM, 80, "myserver");


#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#ifndef BOOT_PIN
#define BOOT_PIN 0
#endif


#define LED_LABEL "The LED pin number"
#define BOOL_LABEL "A bool variable"
#define BOOL_LABEL2 "A second bool variable"
#define LONG_LABEL "A long variable"
#define FLOAT_LABEL "A float variable"
#define STRING_LABEL "A String variable"
#define DROPDOWN_LABEL "Days of week"
#define BRIGHTNESS_LABEL "Brightness"


uint8_t ledPin = LED_BUILTIN;
bool boolVar = true;
bool boolVar2 = false;
uint32_t longVar = 1234567890;
float floatVar = 15.51F;
String stringVar = "Test option String";


const char* days[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
uint8_t daySelected = 2;
AsyncFsWebServer::DropdownList dayOfWeek{ DROPDOWN_LABEL, days, 7, daySelected};


AsyncFsWebServer::Slider brightness{ BRIGHTNESS_LABEL, 0.0, 100.0, 1.0, 50.0 };


static const char reload_btn_htm[] PROGMEM = R"EOF(
<div class="btn-bar">
  <a class="btn" id="reload-btn">Reload options</a>
</div>
)EOF";

static const char reload_btn_script[] PROGMEM = R"EOF(
/* Add click listener to button */
const reloadCfg = () => {
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
};
document.getElementById('reload-btn').addEventListener('click', reloadCfg);
)EOF";
void onConfigSaved(const char* path);
bool startFilesystem();
bool loadOptions();
void handleLoadOptions(AsyncWebServerRequest *request);
void setup();
void loop();
#line 76 "C:/Users/Tolentino/Documents/Arduino/libraries/async-esp-fs-webserver/pio_examples/customOptions/src/customOptions.ino"
void onConfigSaved(const char* path) {
  Serial.print("\n[Config] File salvato: ");
  Serial.println(path);
}


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


bool loadOptions() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    server.getOptionValue(LED_LABEL, ledPin);
    server.getOptionValue(BOOL_LABEL, boolVar);
    server.getOptionValue(BOOL_LABEL2, boolVar2);
    server.getOptionValue(LONG_LABEL, longVar);
    server.getOptionValue(FLOAT_LABEL, floatVar);
    server.getOptionValue(STRING_LABEL, stringVar);
    server.getDropdownSelection(dayOfWeek);
    server.getSliderValue(brightness);
    server.closeSetupConfiguration();

    Serial.println("\nThis are the current values stored: \n");
    Serial.print("LED pin value: "); Serial.println(ledPin);
    Serial.print("Bool value: "); Serial.println(boolVar ? "true" : "false");
    Serial.print("Bool value2: "); Serial.println(boolVar2 ? "true" : "false");
    Serial.print("Long value: "); Serial.println(longVar);
    Serial.print("Float value: "); Serial.println(floatVar);
    Serial.print("String value: "); Serial.println(stringVar);
    Serial.print("Dropdown selected value: "); Serial.println(days[dayOfWeek.selectedIndex]);
    Serial.print("Slider value: "); Serial.println(brightness.value);
    return true;
  }
  else
    Serial.println(F("Config file not exist"));
  return false;
}



void handleLoadOptions(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Options loaded");
  loadOptions();
  Serial.println("Application option loaded after web request");
}


void setup() {
  Serial.begin(115200);


  if (startFilesystem()){

    loadOptions();
  }




  String version = "1.0." + String(BUILD_TIMESTAMP);
  server.setFirmwareVersion(version);


  if (!server.startWiFi(10000)) {
    Serial.println("\nWiFi not connected! Starting AP mode...");
    server.startCaptivePortal("ESP_AP", "123456789", "/setup");
  }


  server.on("/reload", HTTP_GET, handleLoadOptions);


  server.addOptionBox("My Options");
  server.addOption(BOOL_LABEL, boolVar);
  server.addOption(BOOL_LABEL2, boolVar2);
  server.addOption(LED_LABEL, ledPin);
  server.addOption(LONG_LABEL, longVar);
  server.addOption(FLOAT_LABEL, floatVar, 1.0, 100.0, 0.01);
  server.addOption(STRING_LABEL, stringVar);
  server.addDropdownList(dayOfWeek);
  server.addSlider(brightness);
  server.addHTML(reload_btn_htm, "buttons", false);
  server.addJavascript(reload_btn_script, "js", false);


  server.enableFsCodeEditor();





  server.setConfigSavedCallback(onConfigSaved);


  server.init();
  Serial.print(F("\nESP Web Server started on IP Address: "));
  Serial.println(server.getServerIP());
  Serial.println(F(
      "\nThis is \"customOptions.ino\" example.\n"
      "Open /setup page to configure optional parameters.\n"
      "Open /edit page to view, edit or upload example or your custom webserver source files."
  ));

  Serial.print(F("Compile time (default firmware version): "));
  Serial.println(BUILD_TIMESTAMP);
}

void loop() {
  if (server.isAccessPointMode())
    server.updateDNS();


  static uint32_t buttonPressStart = 0;
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


  delay(10);
}