#ifndef ASYNC_FS_WEBSERVER_H
#define ASYNC_FS_WEBSERVER_H

#include <FS.h>
#include <ESPAsyncWebServer.h>

#ifdef ESP32
  #include <Update.h>
  #include <esp_wifi.h>
  #include <esp_int_wdt.h>
  #include <esp_task_wdt.h>
#elif defined(ESP8266)
#else
  #include <Updater.h>
    #error Platform not supported
#endif



#define INCLUDE_EDIT_HTM
#ifdef INCLUDE_EDIT_HTM
#include "edit_htm.h"
#endif

#define INCLUDE_SETUP_HTM
#ifdef INCLUDE_SETUP_HTM
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include "setup_htm.h"
#define CONFIG_FOLDER "/config"
#define CONFIG_FILE "/config.json"
#endif

#define DBG_OUTPUT_PORT Serial
#define DEBUG_MODE_WS 1

#if DEBUG_MODE_WS
#define DebugPrint(x) DBG_OUTPUT_PORT.print(x)
#define DebugPrintln(x) DBG_OUTPUT_PORT.println(x)
#define DebugPrintf(fmt, ...) DBG_OUTPUT_PORT.printf(fmt, ##__VA_ARGS__)
#define DebugPrintf_P(fmt, ...) DBG_OUTPUT_PORT.printf_P(fmt, ##__VA_ARGS__)
#else
#define DebugPrint(x)
#define DebugPrintln(x)
#define DebugPrintf(x, ...)
#define DebugPrintf_P(x, ...)
#endif

#define MIN_F -3.4028235E+38
#define MAX_F 3.4028235E+38
#define MAX_APNAME_LEN 16

typedef struct {
  size_t totalBytes;
  size_t usedBytes;
} fsInfo_t;

using FsInfoCallbackF = std::function<void(fsInfo_t*)>;
using CallbackF = std::function<void(void)>;


class AsyncFsWebServer : public AsyncWebServer
{
  protected:
    // AsyncWebServer* m_server = nullptr;
    AsyncWebSocket* m_ws = nullptr;
    void handleIndex(AsyncWebServerRequest *request);
    void handleWebSocket(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len);
    void handleScanNetworks(AsyncWebServerRequest *request);
    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void doWifiConnection(AsyncWebServerRequest *request);
    bool handleFileRead(const String &uri, AsyncWebServerRequest *request);
    void handleSetup(AsyncWebServerRequest *request) ;
    void notFound(AsyncWebServerRequest *request);
    void getStatus(AsyncWebServerRequest *request);
    void clearConfig(AsyncWebServerRequest *request);

    // Get data and then do update
    void update_first(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void update_second(AsyncWebServerRequest *request);

        // edit page, in usefull in some situation, but if you need to provide only a web interface, you can disable
#ifdef INCLUDE_EDIT_HTM
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleFsStatus(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
#endif

  /**
   * Redirect to captive portal if we got a request for another domain.
  */
  bool captivePortal(AsyncWebServerRequest *request);

  private:
    fs::FS* m_filesystem = nullptr;
    bool m_apmode = false;
    uint32_t m_timeout = 10000;
    uint8_t numOptions = 0;
    char m_version[16] = {__TIME__};
    bool m_filesystem_ok = false;
    char m_apWebpage[MAX_APNAME_LEN] = "/setup";
    size_t m_contentLen = 0;
    FsInfoCallbackF getFsInfo = nullptr;

  public:
    AsyncFsWebServer(uint16_t port, fs::FS &fs) : AsyncWebServer(port) {
      m_ws = new AsyncWebSocket("/ws");
      m_filesystem = &fs;
    }

    ~AsyncFsWebServer() {
      reset();
      end();
      if(_catchAllHandler) delete _catchAllHandler;
    }

    // AsyncWebServer* getServer() { return m_server;}
    AsyncWebSocket* getWebSocket() { return m_ws;}

    void wsBroadcast(const char * buffer) {
      m_ws->textAll(buffer);
    }


    void setCaptiveWebage(const char *url){
      strncpy(m_apWebpage, url, MAX_APNAME_LEN);
    }

    /*
      Start webserver aand bind a websocket event handler (optional)
    */
    bool init(AwsEventHandler wsHandle = nullptr);

    /*
      Enable the built-in ACE web file editor
    */
    void enableFsCodeEditor();

    /*
    * Get reference to current config.json file
    */
    File getConfigFile(const char* mode) {
      File file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, mode);
      return file;
    }

    const char* getConfiFileName() {
      return CONFIG_FOLDER CONFIG_FILE;
    }

    /*
    * Set current firmware version (shown in /setup webpage)
    */
    void setFirmwareVersion(char* version) {
      strncpy(m_version, version, sizeof(m_version));
    }

    /*
    * Set /setup webpage title
    */
    void setSetupPageTitle(const char* title) {
      addOption("name-logo", title);
    }

    /*
    * Set callback function to provide updated FS info to library
    * This it is necessary due to the different implementation of
    * libraries for the filesystem (LittleFS, FFat, SPIFFS etc etc)
    */
    void setFsInfoCallback(FsInfoCallbackF fsCallback) {
      getFsInfo = fsCallback;
    }

    /*
      Send a default "OK" reply to client
    */
    void sendOK(AsyncWebServerRequest *request);

    /*
      Set the WiFi mode as Access Point
    */
    IPAddress setAPmode(const char *ssid, const char *psk);
    /*
      Start WiFi connection, if fails to in AP mode
    */
    IPAddress startWiFi(uint32_t timeout, const char *apSSID, const char *apPsw, CallbackF fn=nullptr);

    /*
      Read the given file from the filesystem and stream it back to the client
    */
    String getContentType(const String& path);

    /*
      In order to keep config.json file small and clean, custom HTML, CSS and Javascript
      will be saved as file. The related option will contain the path to this file
    */
    bool optionToFile(const char* filename, const char* str, bool overWrite);
    /*
      Add an option which contain "raw" HTML code to be injected in /setup page
      Th HTML code will be written in a file with named as option id
    */
    void addHTML(const char* html, const char* id, bool overWrite = false) ;
    /*
      Add an option which contain "raw" CSS style to be injected in /setup page
      Th CSS code will be written in a file with named as option raw-css.css
    */
    void addCSS(const char* css, bool overWrite = false);
    /*
      Add an option which contain "raw" JS script to be injected in /setup page
      Th JS code will be written in a file with named as option raw-javascript.js
    */
    void addJavascript(const char* script, bool overWrite = false) ;
    /*
      Add a new option box with custom label
    */
    void addDropdownList(const char *label, const char** array, size_t size);

    /*
      Add a new option box with custom label
    */
    inline void addOptionBox(const char* boxTitle) {
      addOption("param-box", boxTitle, false);
    }
    /*
      Add custom option to config webpage (float values)
    */
    template <typename T>
    inline void addOption(const char *label, T val, double d_min, double d_max, double step) {
      addOption(label, val, false, d_min, d_max, step);
    }
    /*
      Add custom option to config webpage (type of parameter will be deduced from variable itself)
    */
    template <typename T>
    inline void addOption(const char *label, T val, bool hidden = false,
                          double d_min = MIN_F, double d_max = MAX_F, double step = 1.0) {
      File file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, "r");
      int sz = file.size() * 1.33;
      int docSize = max(sz, 2048);
      DynamicJsonDocument doc((size_t)docSize);
      if (file) {
        // If file is present, load actual configuration
        DeserializationError error = deserializeJson(doc, file);
        if (error) {
          DebugPrintln(F("Failed to deserialize file, may be corrupted"));
          DebugPrintln(error.c_str());
          file.close();
          return;
        }
        file.close();
      }
      else DebugPrintln(F("File not found, will be created new configuration file"));
      numOptions++ ;
      String key = label;
      if (hidden)
        key += "-hidden";
      // Univoque key name
      if (key.equals("param-box"))
        key += numOptions ;
      if (key.equals("raw-javascript"))
        key += numOptions ;

      // If key is present in json, we don't need to create it.
      if (doc.containsKey(key.c_str()))
        return;

      // if min, max, step != from default, treat this as object in order to set other properties
      if (d_min != MIN_F || d_max != MAX_F || step != 1.0) {
        JsonObject obj = doc.createNestedObject(key);
        obj["value"] = static_cast<T>(val);
        obj["min"] = d_min;
        obj["max"] = d_max;
        obj["step"] = step;
      }
      else {
        doc[key] = static_cast<T>(val);
      }

      file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, "w");
      if (serializeJsonPretty(doc, file) == 0)
        DebugPrintln(F("Failed to write to file"));
      file.close();
    }
    /*
      Get current value for a specific custom option (true on success)
    */
    template <typename T>
    inline bool getOptionValue(const char *label, T &var) {
      File file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, "r");
      DynamicJsonDocument doc(file.size() * 1.33);
      if (file) {
        DeserializationError error = deserializeJson(doc, file);
        if (error) {
          DebugPrintln(F("Failed to deserialize file, may be corrupted"));
          DebugPrintln(error.c_str());
          file.close();
          return false;
        }
        file.close();
      }
      else
        return false;

      if (doc[label]["value"])
        var = doc[label]["value"].as<T>();
      else if (doc[label]["selected"])
        var = doc[label]["selected"].as<T>();
      else
        var = doc[label].as<T>();
      return true;
    }

};

#endif
