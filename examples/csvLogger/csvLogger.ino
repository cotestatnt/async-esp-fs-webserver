#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

AsyncFsWebServer server(80, LittleFS, "myServer");
bool captiveRun = false;

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
#include <time.h>

struct tm ntpTime;
const char* basePath = "/csv";


////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem(){
  if (LittleFS.begin()){
        server.printFileList(LittleFS, "/", 2);
        return true;
  }
  else {
        Serial.println("ERROR on mounting filesystem. It will be formmatted!");
        LittleFS.format();
        ESP.restart();
  }
  return false;
}

//////////////////////////// Append a row to csv file ///////////////////////////////////
bool appenRow() {

  getLocalTime(&ntpTime, 10);

  char filename[32];
  snprintf(filename, sizeof(filename),
    "%s/%04d_%02d_%02d.csv",
    basePath,
    ntpTime.tm_year + 1900,
    ntpTime.tm_mon + 1,
    ntpTime.tm_mday
  );

  File file;
  if (LittleFS.exists(filename)) {
    file = LittleFS.open(filename, "a");   // Append to existing file
  }
  else {
    file = LittleFS.open(filename, "w");   // Create a new file
    file.println("timestamp, free heap, largest free block, connected, wifi strength");
  }

  if (file) {
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%c", &ntpTime);

    char row[64];
  #ifdef ESP32
      snprintf(row, sizeof(row), "%s, %d, %d, %s, %d",
        timestamp,
        heap_caps_get_free_size(0),
        heap_caps_get_largest_free_block(0),
        (WiFi.status() == WL_CONNECTED) ? "true" : "false",
        WiFi.RSSI()
      );
  #elif defined(ESP8266)
      uint32_t free;
      uint32_t max;
      ESP.getHeapStats(&free, &max, nullptr);
      snprintf(row, sizeof(row),
        "%s, %d, %d, %s, %d",
        timestamp, free, max,
        (WiFi.status() == WL_CONNECTED) ? "true" : "false",
        WiFi.RSSI()
      );
  #endif
    Serial.println(row);
    file.println(row);
    file.close();
    return true;
  }

  return false;
}


void setup() {
    Serial.begin(115200);
    delay(1000);
    startFilesystem();

    IPAddress myIP = server.startWiFi(15000);
    if (!myIP) {
        Serial.println("\n\nNo WiFi connection, start AP and Captive Portal\n");
        myIP = WiFi.softAPIP();
        captiveRun = true;
    }

    // Enable ACE FS file web editor and add FS info callback fucntion
    server.enableFsCodeEditor();
    #ifdef ESP32
    server.setFsInfoCallback([](fsInfo_t* fsInfo) {
        fsInfo->totalBytes = LittleFS.totalBytes();
        fsInfo->usedBytes = LittleFS.usedBytes();
        fsInfo->fsName = "LittleFS";
    });
    #endif

    // Start server
    server.init();
    Serial.print(F("Async ESP Web Server started on IP Address: "));
    Serial.println(myIP);
    Serial.println(F(
        "This is \"scvLogger.ino\" example.\n"
        "Open /setup page to configure optional parameters.\n"
        "Open /edit page to view, edit or upload example or your custom webserver source files."
    ));

    // Set NTP servers
    #ifdef ESP8266
    configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
    #elif defined(ESP32)
    configTzTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
    #endif
    // Wait for NTP sync (with timeout)
    getLocalTime(&ntpTime, 5000);
  

    // Create csv logs folder if not exists
    if (!LittleFS.exists(basePath)) {
      LittleFS.mkdir(basePath);
    }
}

void loop() {
    if (captiveRun)
        server.updateDNS();

    static uint32_t updateTime;
    if (millis()- updateTime > 30000) {
        updateTime = millis();
        appenRow();
    }
}
