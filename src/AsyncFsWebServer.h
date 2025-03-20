#ifndef ASYNC_FS_WEBSERVER_H
#define ASYNC_FS_WEBSERVER_H

#include <FS.h>
#include <DNSServer.h>
#include "SerialLog.h"
#include "ESPAsyncWebServer.h"

#ifdef ESP32
  #include <WiFi.h>
  #include <WiFiAP.h>
  #include <Update.h>
  #include <ESPmDNS.h>
  #include "esp_wifi.h"
  #include "esp_task_wdt.h"
  #include "sys/stat.h"
#elif defined(ESP8266)
  #include <ESP8266mDNS.h>
  #include <Updater.h>
#else
  #error Platform not supported
#endif

#ifndef ESP_FS_WS_EDIT
    #define ESP_FS_WS_EDIT              1   //has edit methods
    #ifndef ESP_FS_WS_EDIT_HTM
        #define ESP_FS_WS_EDIT_HTM      1   //included from progmem
    #endif
#endif

#ifndef ESP_FS_WS_SETUP
    #define ESP_FS_WS_SETUP             1   //has setup methods
    #ifndef ESP_FS_WS_SETUP_HTM
        #define ESP_FS_WS_SETUP_HTM     1   //included from progmem
    #endif
#endif

#if ESP_FS_WS_EDIT_HTM
    #include "edit_htm.h"
#endif

#if ESP_FS_WS_SETUP_HTM
    #define ESP_FS_WS_CONFIG_FOLDER "/config"
    #define ESP_FS_WS_CONFIG_FILE ESP_FS_WS_CONFIG_FOLDER "/config.json"
    #include "setup_htm.h"
    #include "SetupConfig.hpp"
#endif

#define ARDUINOJSON_USE_LONG_LONG 1
    #include <ArduinoJson.h>
#if ARDUINOJSON_VERSION_MAJOR > 6
    #define JSON_DOC(x) JsonDocument doc
#else
    #define JSON_DOC(x) DynamicJsonDocument doc((size_t)x)
#endif
#include "CaptivePortal.hpp"

#define LIB_URL "https://github.com/cotestatnt/async-esp-fs-webserver/"
#define MIN_F -3.4028235E+38
#define MAX_F 3.4028235E+38

// Watchdog timeout utility
#if defined(ESP32)
    #define AWS_WDT_TIMEOUT (CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000)
    #define AWS_LONG_WDT_TIMEOUT (AWS_WDT_TIMEOUT * 4)
#else
  #define AWS_WDT_TIMEOUT 5000
  #define AWS_LONG_WDT_TIMEOUT 15000
#endif

typedef struct {
  size_t totalBytes;
  size_t usedBytes;
  String fsName;
} fsInfo_t;

using FsInfoCallbackF = std::function<void(fsInfo_t*)>;
using CallbackF = std::function<void(void)>;

class AsyncFsWebServer : public AsyncWebServer
{
  protected:
    AsyncWebSocket* m_ws = nullptr;
    AsyncWebHandler *m_captive = nullptr;
    DNSServer* m_dnsServer = nullptr;

    void handleWebSocket(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len);
    void handleScanNetworks(AsyncWebServerRequest *request);
    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void doWifiConnection(AsyncWebServerRequest *request);

    void notFound(AsyncWebServerRequest *request);
    void handleSetup(AsyncWebServerRequest *request);
    void getStatus(AsyncWebServerRequest *request);
    void clearConfig(AsyncWebServerRequest *request);
    void handleFileName(AsyncWebServerRequest *request);

    // Get data and then do update
    void onUpdate();

    // edit page, in useful in some situation, but if you need to provide only a web interface, you can disable
#if ESP_FS_WS_EDIT_HTM
    void deleteContent(String& path) ;
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFsStatus(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileEdit(AsyncWebServerRequest *request);
#endif

  /*
    Create a dir if not exist on uploading files
  */
  bool createDirFromPath( const String& path) ;

  private:
    char* m_pageUser = nullptr;
    char* m_pagePswd = nullptr;
    String m_host = "esphost";
    String m_captiveUrl = "/setup";

    uint16_t m_port;
    uint32_t m_timeout = AWS_LONG_WDT_TIMEOUT;

    char m_version[16] = {__TIME__};
    bool m_filesystem_ok = false;

    fs::FS* m_filesystem = nullptr;
    FsInfoCallbackF getFsInfo = nullptr;

    IPAddress m_serverIp = IPAddress(192, 168, 4, 1);

#if ESP_FS_WS_SETUP
    SetupConfigurator* setup = nullptr;
#endif

  public:
    AsyncFsWebServer(uint16_t port, fs::FS &fs, const char* hostname = "") :
    AsyncWebServer(port),
    m_filesystem(&fs)
    {
      m_port = port;
#if ESP_FS_WS_SETUP
    setup = new SetupConfigurator(m_filesystem);
#endif
      m_ws = new AsyncWebSocket("/ws");
      if (strlen(hostname))
        m_host = hostname;
    }

    ~AsyncFsWebServer() {
      reset();
      end();
      if(_catchAllHandler) delete _catchAllHandler;
    }

  #ifdef ESP32
    inline TaskHandle_t getTaskHandler() {
      return xTaskGetCurrentTaskHandle();
    }
  #endif

    /*
      Get the webserver IP address
    */
    inline IPAddress getServerIP() {
      return m_serverIp;
    }
    /*
      Start webserver and bind a websocket event handler (optional)
    */
    bool init(AwsEventHandler wsHandle = nullptr);

    /*
      Enable the built-in ACE web file editor
    */
    void enableFsCodeEditor();

    /*
      Enable authenticate for /setup webpage
    */
    void setAuthentication(const char* user, const char* pswd);

    /*
      List FS content
    */
    void printFileList(fs::FS &fs, const char * dirname, uint8_t levels);

    /*
      Send a default "OK" reply to client
    */
    void sendOK(AsyncWebServerRequest *request);

    /*
      Start WiFi connection, callback function is called when trying to connect
    */
    bool startWiFi(uint32_t timeout, CallbackF fn=nullptr) ;


    [[deprecated("Use startWiFi(timeout) and if it fails, use startCaptivePortal(ssid, pswd) instead.")]]
    IPAddress startWiFi(uint32_t timeout, const char* ssid, const char* pswd, CallbackF fn = nullptr, const char* redirectTargetURL = nullptr) {
      if (!startWiFi(timeout, fn)) {
        delay(100);
        startCaptivePortal(ssid, pswd, redirectTargetURL == nullptr ? "/setup" : redirectTargetURL);
      }
      return m_serverIp;
    }

    /*
     * Redirect to captive portal if we got a request for another domain.
    */
    bool startCaptivePortal(const char* ssid, const char* pass, const char* redirectTargetURL);

    /*
     * get instance of current websocket handler
    */
    AsyncWebSocket* getWebSocket() { return m_ws;}

    /*
     * Broadcast a websocket message to all clients connected
    */
    void wsBroadcast(const char * buffer) {
      m_ws->textAll(buffer);
    }

    /*
    * Broadcast a binary websocket message to all clients connected
    */
    void wsBroadcastBinary(uint8_t * message, size_t len) {
      m_ws->binaryAll(message, len);
    }

    /*
    * Need to be run in loop to handle DNS requests
    */
    inline void updateDNS() {
      m_dnsServer->processNextRequest();
    }

    /*
    * Set callback function to provide updated FS info to library
    * This it is necessary due to the different implementation of
    * libraries for the filesystem (LittleFS, FFat, SPIFFS etc etc)
    */
    inline void setFsInfoCallback(FsInfoCallbackF fsCallback) {
      getFsInfo = fsCallback;
    }

    /*
    * Set current firmware version (shown in /setup webpage)
    */
    inline void setFirmwareVersion(char* version) {
      strlcpy(m_version, version, sizeof(m_version));
    }

    /*
    * Set hostmane
    */
    inline void setHostname(const char * host) {
      m_host = host;
    }

    /*
    * Get current library version
    */
    const char* getVersion();

    /////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////   SETUP PAGE CONFIGURATION /////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
#if ESP_FS_WS_SETUP
    /*
    * Get reference to current config.json file
    */
    inline File getConfigFile(const char* mode) {
      File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, mode);
      return file;
    }

    /*
    * Get complete path of config.json file
    */
    inline const char* getConfiFileName() {
      return ESP_FS_WS_CONFIG_FILE;
    }

    void setSetupPageTitle(const char* title) { setup->addOption("name-logo", title); }
    void addHTML(const char* html, const char* id, bool ow = false) {setup->addHTML(html, id, ow);}
    void addCSS(const char* css, const char* id, bool ow = false){setup->addCSS(css, id, ow);}
    void addJavascript(const char* script, const char* id, bool ow = false) {setup->addJavascript(script, id, ow);}
    void addDropdownList(const char *lbl, const char** a, size_t size){setup->addDropdownList(lbl, a, size);}
    void addOptionBox(const char* title) { setup->addOption("param-box", title); }
    void setLogoBase64(const char* logo, const char* w = "128", const char* h = "128", bool ow = false) {
      setup->setLogoBase64(logo, w, h, ow);
    }
    template <typename T>
    void addOption(const char *lbl, T val, double min, double max, double st){
      setup->addOption(lbl, val, false, min, max, st);
    }
    template <typename T>
    void addOption(const char *lbl, T val, bool hidden = false,  double min = MIN_F,
      double max = MAX_F, double st = 1.0) {
      setup->addOption(lbl, val, hidden, min, max, st);
    }
    template <typename T>
    bool getOptionValue(const char *lbl, T &var) { return setup->getOptionValue(lbl, var);}
    template <typename T>
    bool saveOptionValue(const char *lbl, T val) { return setup->saveOptionValue(lbl, val);}
    /////////////////////////////////////////////////////////////////////////////////////////////////
#endif

};

#endif
