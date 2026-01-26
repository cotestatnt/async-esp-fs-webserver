#ifndef ASYNC_FS_WEBSERVER_H
#define ASYNC_FS_WEBSERVER_H

#include <FS.h>
#include <DNSServer.h>

#include "ESPAsyncWebServer.h"
#include "Json.h"
#include "WiFiService.h"
#include "SerialLog.h"
#include "Version.h"

class Print;

#ifdef ESP32
  // Arduino-ESP32 v3 splits networking primitives into a dedicated core library.
  // PlatformIO's dependency finder doesn't always pull it in via transitive includes,
  // so include it explicitly to ensure it gets compiled and linked.
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    #include <Network.h>
  #endif
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
    #define ESP_FS_WS_EDIT              1   // Library has edit methods
    #ifndef ESP_FS_WS_EDIT_HTM
                                            // Disable if you provide your own edit page
        #define ESP_FS_WS_EDIT_HTM      1   // Library serve /edit webpage from progmem                                            
    #endif
#endif

#ifndef ESP_FS_WS_SETUP
    #define ESP_FS_WS_SETUP             1   // Library has setup methods
    #ifndef ESP_FS_WS_SETUP_HTM
                                            // Disable if you provide your own setup page
        #define ESP_FS_WS_SETUP_HTM     1   // Library serve /setup webpage from progmem                                            
    #endif
#endif

#if ESP_FS_WS_EDIT_HTM
    #include "assets/edit_htm.h"
#endif

#if ESP_FS_WS_SETUP_HTM
    #define ESP_FS_WS_CONFIG_FOLDER "/config"
    #define ESP_FS_WS_CONFIG_FILE ESP_FS_WS_CONFIG_FOLDER "/config.json"
    #include "assets/setup_htm.h"
    #include "assets/creds_js.h"
    #include "CredentialManager.h"    
    #include "SetupConfig.hpp" 
#endif

#include "CaptivePortal.hpp"
#ifndef ESP_FS_WS_MDNS
  #define ESP_FS_WS_MDNS 1
#endif


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
using ConfigSavedCallbackF = std::function<void(const char*)>;  // Callback for config file saves

class AsyncFsWebServer : public AsyncWebServer
{
  protected:
    AsyncWebSocket* m_ws = nullptr;
    AsyncWebHandler *m_captive = nullptr;
    DNSServer* m_dnsServer = nullptr;

    char m_apSSID[33] = {0};
    char m_apPassword[65] = {0};
    bool m_isApMode = false;
    bool m_authAll = false;
  
    void notFound(AsyncWebServerRequest *request);
    void handleFileName(AsyncWebServerRequest *request);

#if ESP_FS_WS_SETUP    
    void handleSetup(AsyncWebServerRequest *request);
    void getStatus(AsyncWebServerRequest *request);
    void clearConfig(AsyncWebServerRequest *request);        
    void doWifiConnection(AsyncWebServerRequest *request);
    void handleScanNetworks(AsyncWebServerRequest *request);
    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);    
    void onUpdate();
#endif

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
    String typeName = "FileSystem";

    uint16_t m_port;
    uint32_t m_timeout = AWS_LONG_WDT_TIMEOUT;

    // Firmware version buffer (expanded to accommodate custom version strings)
    String m_version;
    bool m_filesystem_ok = false;

    fs::FS* m_filesystem = nullptr;
    FsInfoCallbackF getFsInfo = nullptr;
    ConfigSavedCallbackF m_configSavedCallback = nullptr;  // Callback for config file saves

    IPAddress m_serverIp = IPAddress(192, 168, 4, 1);
    CredentialManager *m_credentialManager = nullptr;

#if ESP_FS_WS_SETUP
    SetupConfigurator* setup = nullptr;
    
    // Lazy initialization: create setup object only when first needed
    SetupConfigurator* getSetupConfigurator() {
      if (!setup) {
        setup = new SetupConfigurator(m_filesystem);
      }
      return setup;
    }
#endif

  public:
    
    // Template Constructor for derived filesystem classes (LittleFS, SPIFFS, etc)
    template <typename T>
    AsyncFsWebServer(T &fs, uint16_t port, const char* hostname = "") : AsyncWebServer(port), m_filesystem(&fs)
    {
      m_port = port;      

      if (!m_credentialManager) {
        log_error("Credential manager not initialized");        
        m_credentialManager = new CredentialManager();
        m_credentialManager->begin();
        #ifdef ESP8266        
        m_credentialManager->setFilesystem(m_filesystem);  // Set FS for persistence
        #endif
      }     

      // Set hostname if provided from constructor
      if (strlen(hostname))
        m_host = hostname;
      
#ifdef ESP32
        // Auto-configure getFsInfo for ESP32 filesystems
        // Try to infer filesystem name from template type
        String pretty = __PRETTY_FUNCTION__;
        int start = pretty.indexOf("T = ");
        if (start != -1)
        {
            start += 4;
            int end = pretty.indexOf("]", start);
            int semi = pretty.indexOf(";", start);
            if (semi != -1 && semi < end)
            {
                end = semi;
            }
            if (end != -1)
            {
                typeName = pretty.substring(start, end);
                // Clean up common prefixes/suffixes
                typeName.replace("fs::", "");
                if (typeName.endsWith("FS"))
                {
                    typeName = typeName.substring(0, typeName.length() - 2);
                }
            }
        }

        getFsInfo = [&fs, this](fsInfo_t *info)
        {
            info->totalBytes = fs.totalBytes();
            info->usedBytes = fs.usedBytes();
            info->fsName = this->typeName;
        };
#endif    
    }

    // Class destructor
    ~AsyncFsWebServer() {
      reset();
      end();
      if(_catchAllHandler) delete _catchAllHandler;
      
      // Deallocate all dynamically allocated resources
      if(m_pageUser) delete[] m_pageUser;
      if(m_pagePswd) delete[] m_pagePswd;
      if(m_ws) delete m_ws;
      if(m_captive) delete m_captive;
      if(m_dnsServer) delete m_dnsServer;
      if (m_credentialManager) delete m_credentialManager;
#if ESP_FS_WS_SETUP
      if(setup) delete setup;  // Only delete if it was lazily initialized
#endif
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
      Return true if the device is currently running in Access Point mode
    */
    inline bool isAccessPointMode() const { return m_isApMode; }
    /*
      Start webserver and bind a websocket event handler (optional)
    */
    bool init(AwsEventHandler wsHandle = nullptr);

#if ESP_FS_WS_EDIT

    /*
      Enable the built-in ACE web file editor
    */
    void enableFsCodeEditor();

    // Backward compatibility method
    [[deprecated("Use enableFsCodeEditor() instead (use built-in callback to provide FS info).")]]
    void enableFsCodeEditor(FsInfoCallbackF fsCallback) {
        if (fsCallback)
            getFsInfo = fsCallback;
        enableFsCodeEditor();
    }

    /*
     * Set callback function to provide updated FS info to library
     * This it is necessary due to the different implementation of
     * libraries for the filesystem (LittleFS, FFat, SPIFFS etc etc)
     */
    [[deprecated("Use enableFsCodeEditor() instead (use built-in callback to provide FS info)")]]
    inline void setFsInfoCallback(FsInfoCallbackF fsCallback)
    {
        getFsInfo = fsCallback;
    }
#endif

    /*
      Enable authenticate for /setup webpage
    */
    void setAuthentication(const char* user, const char* pswd);
    /*
    Enable the flag which turns on basic authentication for all pages
    */
    inline void requireAuthentication(bool require){
      m_authAll = require;
    }

    /*
      List FS content
    */
    void printFileList(fs::FS &fs, const char * dirname, uint8_t levels);

    /*
      List FS content to a destination stream (e.g. Serial, WiFiClient)
    */
    void printFileList(fs::FS &fs, const char * dirname, uint8_t levels, Print& out);

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
    bool startCaptivePortal(const char* ssid, const char* pass, const char* redirectTargetURL = "/setup");
    bool startCaptivePortal(WiFiConnectParams& params, const char *redirectTargetURL = "/setup");

    /*
      Set AP SSID and Password (backward compatibility)
    */
    void setAP(const char *ssid, const char *pass) {
      strlcpy(m_apSSID, ssid, sizeof(m_apSSID));
      strlcpy(m_apPassword, pass, sizeof(m_apPassword));
    }

    /*
    * Setup and start mDNS responder
    */
    bool startMDNSResponder();

    /*
    * Need to be run in loop to handle DNS requests
    */
    inline void updateDNS() {
      m_dnsServer->processNextRequest();
    }

    /*
     * get instance of current websocket handler (enabled at runtime)
    */
    AsyncWebSocket* getWebSocket() { return m_ws; }

    /*
     * Enable WebSocket at runtime. Creates WS on `path` and registers handler.
    */
    void enableWebSocket(const char* path, AwsEventHandler handler);

        /*
     * Broadcast a websocket message to all clients connected
    */
    void wsBroadcast(const char * buffer) {
      if (m_ws != nullptr)
        m_ws->textAll(buffer);
    }

    /*
    * Broadcast a binary websocket message to all clients connected
    */
    void wsBroadcastBinary(uint8_t * message, size_t len) {
      if (m_ws != nullptr)
        m_ws->binaryAll(message, len);
    }

    /*
    * Set current firmware version (shown in /setup webpage)
    */
    inline void setFirmwareVersion(const char* version) {
      m_version = String(version);
    }

    inline void setFirmwareVersion(const String& version) {
      m_version = version;
    }

    /*
    * Set hostmane
    */
    inline void setHostname(const char * host) {
      m_host = host;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////   SETUP PAGE CONFIGURATION /////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
#if ESP_FS_WS_SETUP    

    // Public alias to dropdown definition type (available only when /setup is enabled)
    using DropdownList = AsyncFSWebServer::DropdownList;
    // Public alias to slider definition type
    using Slider = AsyncFSWebServer::Slider;

    /*
    * Set callback function to be called when configuration file is saved via /edit POST
    * The callback receives the filename path as parameter
    */
    inline void setConfigSavedCallback(ConfigSavedCallbackF callback) {
      m_configSavedCallback = callback;
    }

    /*
    * Get reference to current config.json file
    */
    inline File getConfigFile(const char* mode) {
      File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, mode);
      return file;
    }

    /*
    * Clear the saved configuration options by removing config.json.
    * Returns true if the file was removed or did not exist.
    */
    inline bool clearConfigFile() {
      if (m_filesystem->exists(ESP_FS_WS_CONFIG_FILE)) {
        return m_filesystem->remove(ESP_FS_WS_CONFIG_FILE);
      }
      return true;
    }

    /*
    * Get complete path of config.json file
    */
    inline const char* getConfiFileName() {
      return ESP_FS_WS_CONFIG_FILE;
    }

    void setSetupPageTitle(const char* title) { getSetupConfigurator()->addOption("name-logo", title); }
    void addHTML(const char* html, const char* id, bool ow = false) {getSetupConfigurator()->addHTML(html, id, ow);}
    void addCSS(const char* css, const char* id, bool ow = false){getSetupConfigurator()->addCSS(css, id, ow);}
    void addJavascript(const char* script, const char* id, bool ow = false) {getSetupConfigurator()->addJavascript(script, id, ow);}
    void addDropdownList(const char *lbl, const char** a, size_t size){getSetupConfigurator()->addDropdownList(lbl, a, size);}    
    void addDropdownList(DropdownList &def){ getSetupConfigurator()->addDropdownList(def); }
    void addSlider(Slider &def){ getSetupConfigurator()->addSlider(def); }
    void addOptionBox(const char* title) { getSetupConfigurator()->addOption("param-box", title); }
    void setLogoBase64(const char* logo, const char* w = "128", const char* h = "128", bool ow = false) {
      getSetupConfigurator()->setLogoBase64(logo, w, h, ow);
    }
    template <typename T>
    void addOption(const char *lbl, T val, double min, double max, double st){
      getSetupConfigurator()->addOption(lbl, val, false, min, max, st);
    }
    template <typename T>
    void addOption(const char *lbl, T val, bool hidden = false,  double min = MIN_F,
      double max = MAX_F, double st = 1.0) {
      getSetupConfigurator()->addOption(lbl, val, hidden, min, max, st);
    }
    template <typename T>
    bool getOptionValue(const char *lbl, T &var) { return getSetupConfigurator()->getOptionValue(lbl, var);}
    template <typename T>
    bool saveOptionValue(const char *lbl, T val) { return getSetupConfigurator()->saveOptionValue(lbl, val);}

    // Update a dropdown definition's selectedIndex from persisted config
    bool getDropdownSelection(DropdownList &def) { return getSetupConfigurator()->getDropdownSelection(def); }
    // Read slider value back into struct
    bool getSliderValue(Slider &def) { return getSetupConfigurator()->getSliderValue(def); }

    void closeSetupConfiguration() {
      getSetupConfigurator()->closeConfiguration();
    }
    /////////////////////////////////////////////////////////////////////////////////////////////////
#endif

};




#endif
