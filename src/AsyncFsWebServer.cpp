#include "AsyncFsWebServer.h"


void setTaskWdt(uint32_t timeout) {
  #if defined(ESP32)
      #if ESP_ARDUINO_VERSION_MAJOR > 2
      esp_task_wdt_config_t twdt_config = {
          .timeout_ms = timeout,
          .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
          .trigger_panic = false,
      };
      ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
      #else
      ESP_ERROR_CHECK(esp_task_wdt_init(timeout / 1000, 0));
      #endif
  #elif defined(ESP8266)
      ESP.wdtDisable();
      ESP.wdtEnable(timeout);
  #endif
  }


bool AsyncFsWebServer::init(AwsEventHandler wsHandle) {
    // Set build date as default firmware version (YYMMDDHHmm) from Version.h constexprs
    if (m_version.length() == 0)
        m_version = String(BUILD_TIMESTAMP);

//////////////////////    BUILT-IN HANDLERS    ////////////////////////////
    on("*", HTTP_HEAD, [this](AsyncWebServerRequest *request) { this->handleFileName(request); });

#if ESP_FS_WS_SETUP
    m_filesystem_ok = getSetupConfigurator()->checkConfigFile();
    if (getSetupConfigurator()->isOpened()) {
        log_debug("Config file %s closed", ESP_FS_WS_CONFIG_FILE);
        getSetupConfigurator()->closeConfiguration();
    }
    onUpdate();

    on("/setup", HTTP_GET, [this](AsyncWebServerRequest *request) { this->handleSetup(request); });
    on("/connect", HTTP_POST, [this](AsyncWebServerRequest *request) { this->doWifiConnection(request); });
    on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) { this->handleScanNetworks(request); });
    on("/getStatus", HTTP_GET, [this](AsyncWebServerRequest *request) { this->getStatus(request); });
    on("/clear_config", HTTP_GET, [this](AsyncWebServerRequest *request) { this->clearConfig(request); });
    // Simple WiFi status endpoint
    on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncFSWebServer::Json doc;
        doc.setString("ssid", WiFi.SSID());
        doc.setNumber("rssi", WiFi.RSSI());
        request->send(200, "application/json", doc.serialize());
    });
    // File upload handler for configuration files
    on("/upload", HTTP_POST,
        [this](AsyncWebServerRequest *request) { this->sendOK(request); },
        [this](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
            this->handleUpload(request, filename, index, data, len, final);
        }
    );
    // Endpoint to reset device
    on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Send response and restart AFTER client disconnects to ensure 200 reaches browser
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", WiFi.localIP().toString());
        response->addHeader("Connection", "close");
        request->onDisconnect([]() {
            #if defined(ESP8266)
            ESP.reset();
            #else
            ESP.restart();
            #endif
        });
        request->send(response);
    });
#endif
    
    onNotFound([this](AsyncWebServerRequest *request) { this->notFound(request); });
    serveStatic("/", *m_filesystem, "/").setDefaultFile("index.htm");

    if (wsHandle != nullptr) {
        if (!m_ws) m_ws = new AsyncWebSocket("/ws");
        m_ws->onEvent(wsHandle);
        addHandler(m_ws);
    }
    
    DefaultHeaders::Instance().addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    DefaultHeaders::Instance().addHeader("Pragma", "no-cache");
    DefaultHeaders::Instance().addHeader("Expires", "0");
    begin();

    // MDNS is started only in AP mode (see startCaptivePortal)
    return true;
}

void AsyncFsWebServer::printFileList(fs::FS &fs, const char * dirname, uint8_t levels) {
    printFileList(fs, dirname, levels, Serial);
}

void AsyncFsWebServer::printFileList(fs::FS &fs, const char * dirname, uint8_t levels, Print& out) {
    out.print("\nListing directory: ");
    out.println(dirname);
    File root = fs.open(dirname, "r");
    if (!root) {
        out.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        out.println(" - not a directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
        if (levels) {
            #ifdef ESP32
            printFileList(fs, file.path(), levels - 1, out);
            #elif defined(ESP8266)
            printFileList(fs, file.fullName(), levels - 1, out);
            #endif
        }
        } else {
        String line = "|__ FILE: ";
        if (typeName == "SPIFFS") {
            #ifdef ESP32
            line += file.path();
            #elif defined(ESP8266)
            line += file.fullName();
            #endif
        } else {
            line += file.name();
        }      
        line += " (";
        line += (unsigned long)file.size();
        line += " bytes)";
        out.println(line);
        }
        file = root.openNextFile();
    }
}

#if ESP_FS_WS_EDIT
void AsyncFsWebServer::enableFsCodeEditor(FsInfoCallbackF fsCallback) {
    on("/status", HTTP_GET, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFsStatus(request); });
    on("/list", HTTP_GET, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileList(request); });
    on("/edit", HTTP_PUT, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileCreate(request); });
    on("/edit", HTTP_DELETE, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileDelete(request); });
    on("/edit", HTTP_GET, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileEdit(request); });
    on("/edit", HTTP_POST,
        [this](AsyncWebServerRequest *request) { sendOK(request); },
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) { handleUpload(request, filename, index, data, len, final); }
    );
    getFsInfo = fsCallback;
}
#endif


// Enable WebSocket handler at runtime. Creates WS on `path` and registers default handler.
void AsyncFsWebServer::enableWebSocket(const char* path, AwsEventHandler handler) {
    if (m_ws) return;
    m_ws = new AsyncWebSocket(path);
    if (handler) {
        m_ws->onEvent(handler);
    } 
    addHandler(m_ws);
}


void AsyncFsWebServer::setAuthentication(const char* user, const char* pswd) {
    // Free previous allocations if they exist
    if (m_pageUser) {
        free(m_pageUser);
        m_pageUser = nullptr;
    }
    if (m_pagePswd) {
        free(m_pagePswd);
        m_pagePswd = nullptr;
    }
    
    // Validate input parameters
    if (!user || !pswd || strlen(user) == 0 || strlen(pswd) == 0) {
        log_error("Invalid authentication credentials");
        return;
    }
    
    // Allocate with proper size (+1 for null terminator)
    size_t userLen = strlen(user) + 1;
    size_t pswnLen = strlen(pswd) + 1;
    
    m_pageUser = (char*) malloc(userLen);
    m_pagePswd = (char*) malloc(pswnLen);
    
    if (m_pageUser && m_pagePswd) {
        strncpy(m_pageUser, user, userLen - 1);
        strncpy(m_pagePswd, pswd, pswnLen - 1);
        m_pageUser[userLen - 1] = '\0';
        m_pagePswd[pswnLen - 1] = '\0';
        log_debug("Authentication credentials set successfully");
    } 
    else {
        log_error("Failed to allocate memory for authentication credentials");
        if (m_pageUser) {
            free(m_pageUser);
            m_pageUser = nullptr;
        }
        if (m_pagePswd) {
            free(m_pagePswd);
            m_pagePswd = nullptr;
        }
    }
}

void AsyncFsWebServer::handleFileName(AsyncWebServerRequest *request) {
    if (m_filesystem->exists(request->url()))
        request->send(301, "text/plain", "OK");
    request->send(404, "text/plain", "File not found");
}

void AsyncFsWebServer::sendOK(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "OK");
}

void AsyncFsWebServer::notFound(AsyncWebServerRequest *request) {    
    String _url = request->url();

    // Requested file not found, check if gzipped version exists
    _url += ".gz";      
    if (!m_filesystem->exists(_url)) {
        log_debug("File %s not found, checking for index redirection", request->url().c_str());
        
        // File not found
        if (request->url() == "/" && !m_filesystem->exists("/index.htm") && !m_filesystem->exists("/index.html")) {
            request->redirect("/setup");
            log_debug("Redirecting \"/\" to \"/setup\" (no index file found)");
            return;
        }
    }
    else {
        log_debug("Serving gzipped file for %s", request->url().c_str());
        request->redirect(_url);
        return;
    }

    request->send(404, "text/plain", "AsyncFsWebServer: resource not found");
    log_debug("Resource %s not found", request->url().c_str());
}



#if    ESP_FS_WS_SETUP_HTM
void AsyncFsWebServer::handleSetup(AsyncWebServerRequest *request) {
    if (m_pageUser != nullptr) {
        if(!request->authenticate(m_pageUser, m_pagePswd))
            return request->requestAuthentication();
    }

    // Changed array name to match SEGGER Bin2C output
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)_acsetup_min_htm, sizeof(_acsetup_min_htm));
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("X-Config-File", ESP_FS_WS_CONFIG_FILE);
    request->send(response);
}
#endif

#if ESP_FS_WS_SETUP
void AsyncFsWebServer::getStatus(AsyncWebServerRequest *request) {
    AsyncFSWebServer::Json doc;
    doc.setString("firmware", m_version);
    String mode;
    if (WiFi.status() == WL_CONNECTED) {
        mode = "Station (";
        mode += WiFi.SSID();
        mode += ")";
    } else {
        mode = "Access Point";
    }
    doc.setString("mode", mode);
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc.setString("ip", ip);
    doc.setString("hostname", m_host);
    doc.setString("path", String(ESP_FS_WS_CONFIG_FILE).substring(1));   // remove first '/'
    doc.setString("liburl", LIB_URL);
    String reply = doc.serialize();
    request->send(200, "application/json", reply);
}

void AsyncFsWebServer::clearConfig(AsyncWebServerRequest *request) {
    if (m_filesystem->remove(ESP_FS_WS_CONFIG_FILE))
        request->send(200, "text/plain", "Clear config OK");
    else
        request->send(200, "text/plain", "Clear config not done");
}



void AsyncFsWebServer::handleScanNetworks(AsyncWebServerRequest *request) {
    log_info("Start scan WiFi networks");
    
    int res = WiFi.scanComplete();
    log_debug("WiFi.scanComplete() returned: %d", res);

    // Check for scan in progress or failed
    // ESP32 core 3.3.4+: uses WIFI_SCAN_RUNNING and WIFI_SCAN_FAILED constants
    // Older cores: use -1 and -2 respectively
    #ifdef WIFI_SCAN_RUNNING
        // New core (3.3.4+) - use constants
        if (res == WIFI_SCAN_RUNNING) {
            log_info("Scan still in progress...");
            request->send(200, "application/json", "{\"reload\" : 1}");
            return;
        }
        
        if (res == WIFI_SCAN_FAILED) {
            log_info("Scan failed, starting new scan...");
            WiFi.scanNetworks(true);
            request->send(200, "application/json", "{\"reload\" : 1}");
            return;
        }
    #else
        // Old core - use numeric values
        if (res == -2) {
            log_info("Scan not initiated, starting new scan...");
            WiFi.scanNetworks(true);
            request->send(200, "application/json", "{\"reload\" : 1}");
            return;
        }
        
        if (res == -1) {
            log_info("Scan still in progress...");
            request->send(200, "application/json", "{\"reload\" : 1}");
            return;
        }
    #endif
    
    // res >= 0: Scan completed with res networks found
    if (res >= 0) {
        log_info("Scan completed! Number of networks: %d", res);
        // Build JSON array manually
        String json;
        json.reserve(res * 100);
        json = "[";
        for (int i = 0; i < res; ++i) {
            if (i > 0) json += ",";
            AsyncFSWebServer::Json item;
            item.setNumber("strength", WiFi.RSSI(i));
            item.setString("ssid", WiFi.SSID(i));
            #if defined(ESP8266)
            item.setString("security", AUTH_OPEN ? "none" : "enabled");
            #elif defined(ESP32)
            item.setString("security", WIFI_AUTH_OPEN ? "none" : "enabled");
            #endif
            json += item.serialize();
        }
        json += "]";
        request->send(200, "application/json", json);
        log_debug("%s", json.c_str());

        WiFi.scanDelete();
        // Start a new scan for next request
        WiFi.scanNetworks(true);
        return;
    }
}

bool AsyncFsWebServer::createDirFromPath(const String& path) {
    String dir;
    dir.reserve(path.length());
    int p1 = 0;  int p2 = 0;
    while (p2 != -1) {
        p2 = path.indexOf("/", p1 + 1);
        dir += path.substring(p1, p2);
        // Check if its a valid dir
        if (dir.indexOf(".") == -1) {
            if (!m_filesystem->exists(dir)) {
                if (m_filesystem->mkdir(dir)) {
                    log_info("Folder %s created\n", dir.c_str());
                } else {
                    log_info("Error. Folder %s not created\n", dir.c_str());
                    return false;
                }
            }
        }
        p1 = p2;
    }
    return true;
}

void AsyncFsWebServer::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {

    // DebugPrintln("Handle upload POST");
    if (!index) {
        // Increase task WDT timeout
        setTaskWdt(AWS_LONG_WDT_TIMEOUT);

        // Create folder if necessary (up to max 5 sublevels)
        size_t filenameLen = filename.length();
        
        // Protect against excessively long filenames
        if (filenameLen >= 512) {
            log_error("Filename too long (max 512 bytes): %s", filename.c_str());
            request->_tempFile.close();
            return;
        }
        
        char path[filenameLen + 1];
        strcpy(path, filename.c_str());
        createDirFromPath(path);

        // open the file on first call and store the file handle in the request object
        request->_tempFile = m_filesystem->open(filename, "w");
        log_debug("Upload Start: writing file %s", filename.c_str());
    }

    if (len) {
        // stream the incoming chunk to the opened file
        request->_tempFile.write(data, len);
    }

    if (final) {
        // Restore task WDT timeout
        setTaskWdt(AWS_WDT_TIMEOUT);
        // close the file handle as the upload is now done
        request->_tempFile.close();
        log_debug("Upload complete: %s, size: %zu (index: %zu)", filename.c_str(), index + len, index);
        
        // Call config saved callback if this is the config file
        if (filename == ESP_FS_WS_CONFIG_FILE && m_configSavedCallback) {
            log_debug("Config file saved, calling callback");
            m_configSavedCallback(filename.c_str());
        }
    }
}

void AsyncFsWebServer::doWifiConnection(AsyncWebServerRequest *request) {
    String ssid, pass;
    IPAddress gateway, subnet, local_ip;
    bool config = false,  newSSID = false;
    String resp;

    if (request->hasArg("ip_address") && request->hasArg("subnet") && request->hasArg("gateway")) {
        gateway.fromString(request->arg("gateway"));
        subnet.fromString(request->arg("subnet"));
        local_ip.fromString(request->arg("ip_address"));
        config = true;
    }

    if (request->hasArg("ssid"))
        ssid = request->arg("ssid");

    if (request->hasArg("password"))
        pass = request->arg("password");

    if (request->hasArg("newSSID")) {
        newSSID = true;
    }

    /*
    *  If we are already connected and a new SSID is needed, once the ESP will join the new network,
    *  /setup web page will no longer be able to communicate with ESP and therefore
    *  it will not be possible to inform the user about the new IP address.\
    *  Inform and prompt the user for a confirmation (if OK, the next request will force disconnect variable)
    */
    if (WiFi.status() == WL_CONNECTED && !newSSID && WiFi.getMode() != WIFI_AP) {
        log_debug("WiFi status %d", WiFi.status());
        resp.reserve(512);
        resp  = "ESP is already connected to <b>";
        resp += WiFi.SSID();
        resp += "</b> WiFi!<br>Do you want close this connection and attempt to connect to <b>";
        resp += ssid;
        resp += "</b>?<br><br><i>Note:<br>Flash stored WiFi credentials will be updated.<br>The ESP will no longer be reachable from this web page due to the change of WiFi network.<br>To find out the new IP address, check the serial monitor or your router.<br></i>";
        request->send(200, "application/json", resp);
        return;
    }

    if (request->hasArg("persistent")) {
        if (request->arg("persistent").equals("false")) {
            WiFi.persistent(false);
            #if defined(ESP8266)
                struct station_config stationConf;
                wifi_station_get_config_default(&stationConf);
                // Clear previous configuration
                memset(&stationConf, 0, sizeof(stationConf));
                wifi_station_set_config(&stationConf);
            #elif defined(ESP32)
                wifi_config_t stationConf;
                esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &stationConf);
                if (err == ESP_OK) {
                    // Clear previuos configuration
                    memset(&stationConf, 0, sizeof(stationConf));
                    // Store actual configuration
                    memcpy(&stationConf.sta.ssid, ssid.c_str(), ssid.length());
                    memcpy(&stationConf.sta.password, pass.c_str(), pass.length());
                    err = esp_wifi_set_config(WIFI_IF_STA, &stationConf);
                    if (err) {
                        log_error("Set WiFi config: %s", esp_err_to_name(err));
                    }
                }
            #endif
        }
        else {
            // Store current WiFi configuration in flash
            WiFi.persistent(true);
#if defined(ESP8266)
            struct station_config stationConf;
            wifi_station_get_config_default(&stationConf);
            // Clear previous configuration
            memset(&stationConf, 0, sizeof(stationConf));
            os_memcpy(&stationConf.ssid, ssid.c_str(), ssid.length());
            os_memcpy(&stationConf.password, pass.c_str(), pass.length());
            wifi_set_opmode(STATION_MODE);
            wifi_station_set_config(&stationConf);
#elif defined(ESP32)
            wifi_config_t stationConf;
            esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &stationConf);
            if (err == ESP_OK) {
                // Clear previuos configuration
                memset(&stationConf, 0, sizeof(stationConf));
                // Store actual configuration
                memcpy(&stationConf.sta.ssid, ssid.c_str(), ssid.length());
                memcpy(&stationConf.sta.password, pass.c_str(), pass.length());
                err = esp_wifi_set_config(WIFI_IF_STA, &stationConf);
                if (err) {
                    log_error("Set WiFi config: %s", esp_err_to_name(err));
                }
            }
#endif
        }
    }

    // Connect to the provided SSID
    if (ssid.length() && pass.length()) {
        setTaskWdt(AWS_LONG_WDT_TIMEOUT);
        WiFi.mode(WIFI_AP_STA);

        // Manual connection setup
        if (config) {
            log_info("Manual config WiFi connection with IP: %s", local_ip.toString().c_str());
            if (!WiFi.config(local_ip, gateway, subnet)) {
                log_error("STA Failed to configure");
            }
        }

        Serial.print("\n\n\nConnecting to ");
        Serial.println(ssid);
        WiFi.begin(ssid.c_str(), pass.c_str());

        if (WiFi.status() == WL_CONNECTED && newSSID) {
            log_info("Disconnect from current WiFi network");
            WiFi.disconnect();
            delay(10);
        }

        uint32_t beginTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
            delay(250);
            Serial.print("*");
#if defined(ESP8266)
            ESP.wdtFeed();
#else
            esp_task_wdt_reset();
#endif
            if (millis() - beginTime > m_timeout) {
                request->send(408, "text/plain", "<br><br>Connection timeout!<br>Check password or try to restart ESP.");
                delay(100);
                Serial.println("\nWiFi connect timeout!");;
                break;
            }
        }
        // reply to client
        if (WiFi.status() == WL_CONNECTED) {
            // WiFi.softAPdisconnect();

            m_serverIp = WiFi.localIP();
            Serial.print(F("\nConnected to Wifi! IP address: "));
            Serial.println(m_serverIp);
            String serverLoc = F("http://");
            for (int i = 0; i < 4; i++) {
                if (i) serverLoc += ".";
                serverLoc += m_serverIp[i];
            }
            serverLoc += "/setup";
            resp  = "ESP successfully connected to ";
            resp += ssid;
            resp += " WiFi network. <br><b>Restart ESP now?</b><br><br><i>Note: disconnect your browser from ESP AP and then reload <a href='";
            resp += serverLoc;
            resp += "'>";
            resp += serverLoc;
            resp += "</a> or <a href='http://";
            resp += m_host;
            resp += ".local'>http://";
            resp += m_host;
            resp += ".local</a></i>";

            log_debug("%s", resp.c_str());
            request->send(200, "application/json", resp);
            delay(500);  // Give client time to receive response before system changes
            setTaskWdt(AWS_WDT_TIMEOUT);
            return;
        }
    }
    setTaskWdt(AWS_WDT_TIMEOUT);
    request->send(401, "text/plain", "Wrong credentials provided");
}

void AsyncFsWebServer::onUpdate() {
#if defined(ESP8266)
    on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        // the request handler is triggered after the upload has finished... 
        // create the response, add header, and send response
        String txt = Update.hasError() ?  Update.getErrorString() : "Update Success<br>Restart ESP to load new firmware!\n";
        AsyncWebServerResponse *response = request->beginResponse((Update.hasError()) ? 500 : 200, "text/plain", txt);
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");   
        request->send(response);
    },

    //Upload handler chunks in data
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        // if index == 0 then this is the first frame of data
        if(!index){ 
            DBG_OUTPUT_PORT.printf("UploadStart: %s\n", filename.c_str());
            DBG_OUTPUT_PORT.setDebugOutput(true);
            
            // calculate sketch space required for the update
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if(!Update.begin(maxSketchSpace)){//start with max available size
                Update.printError(DBG_OUTPUT_PORT);
                return request->send(500, "text/plain", "OTA failed to start");
            }
            Update.runAsync(true); // tell the updaterClass to run in async mode
        }

        //Write chunked data to the free sketch space
        if(Update.write(data, len) != len){
            Update.printError(DBG_OUTPUT_PORT);
            return request->send(500, "text/plain", "OTA failed to write chunk");
        }
        
        // if the final flag is set then this is the last frame of data
        if(final){ 
            if(Update.end(true)){ //true to set the size to the current progress
                DBG_OUTPUT_PORT.printf("Update Success: %u bytes written.\nRestart ESP!\n", index + len);
            } 
            else {
                Update.printError(DBG_OUTPUT_PORT);
                return request->send(500, "text/plain", "OTA failed");
            }
            DBG_OUTPUT_PORT.setDebugOutput(false);
        }
    });

#elif defined(ESP32)
    on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        // the request handler is triggered after the upload has finished... 
        // create the response, add header, and send response

        String txt = Update.hasError() ?  Update.errorString() : "Success! Restart ESP to load new firmware!\n";
        AsyncWebServerResponse *response = request->beginResponse((Update.hasError()) ? 500 : 200, "text/plain", txt);
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");   
        request->send(response);        
    },

    //Upload handler chunks in data
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){

        if (!index) {                
            log_info("Firmware size: %d", request->getHeader("Content-Length")->value().length());
                    
            // Increase task WDT timeout
            setTaskWdt(AWS_LONG_WDT_TIMEOUT);
            if (!Update.begin()) {
                log_error("Update begin failed");
                Update.printError(DBG_OUTPUT_PORT);
                return request->send(500, "text/plain", "OTA could not begin");
            }                        
        }

        // Write chunked data to the free sketch space
        if (len){   
            esp_task_wdt_reset();                 
            if (Update.write(data, len) != len) {
                log_error("Update write failed");
                return request->send(500, "text/plain", "OTA write failed");
            }
        }

        // if the final flag is set then this is the last frame of data
        if (final) {
            log_info("Update End.");
            if (!Update.end(true)) { //true to set the size to the current progress
                log_error("%s\n", Update.errorString());    
                setTaskWdt(AWS_WDT_TIMEOUT);
                return request->send(500, "text/plain", "Could not end OTA");
            }
            log_info("Update Success.\nRestart ESP!\n");
            setTaskWdt(AWS_WDT_TIMEOUT);
        }
    });
#endif
}

#endif //ESP_FS_WS_SETUP

bool AsyncFsWebServer::startWiFi(uint32_t timeout, CallbackF fn) {
    // Check if we need to config wifi connection
    IPAddress local_ip, subnet, gateway;

#if ESP_FS_WS_SETUP
    File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "r");
    if (file) {
        // If file is present, load actual configuration
        AsyncFSWebServer::Json doc;
        String content = "";
        while (file.available()) {
            content += (char)file.read();
        }
        file.close();
        if (doc.parse(content)) {
            bool dhcp = false;
            if (doc.getBool("dhcp", dhcp) && dhcp == true) {
                String gw, sn, ip;
                if (doc.getString("gateway", gw)) gateway.fromString(gw);
                if (doc.getString("subnet", sn)) subnet.fromString(sn);
                if (doc.getString("ip_address", ip)) local_ip.fromString(ip);
                log_info("Manual config WiFi connection with IP: %s\n", local_ip.toString().c_str());
                if (!WiFi.config(local_ip, gateway, subnet)) {
                    log_error("STA Failed to configure");
                }
                delay(100);
            }
        } else {
            log_error("Failed to parse WiFi config file");
        }
    }
#endif

    WiFi.mode(WIFI_STA);
#if defined(ESP8266)
    struct station_config conf;
    wifi_station_get_config_default(&conf);
    const char* _ssid = reinterpret_cast<const char*>(conf.ssid);
    const char* _pass = reinterpret_cast<const char*>(conf.password);
#elif defined(ESP32)
    wifi_config_t conf;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &conf);
    if (err) {
        log_error("Get WiFi config: %s", esp_err_to_name(err));
        return false;
    }
    const char* _ssid = reinterpret_cast<const char*>(conf.sta.ssid);
    const char* _pass = reinterpret_cast<const char*>(conf.sta.password);
#endif

    if (strlen(_ssid) && strlen(_pass)) {
        WiFi.begin(_ssid, _pass);
        log_debug("Connecting to %s, %s", _ssid, _pass);

        // Will try for some time
        int tryDelay = timeout / 10;
        int numberOfTries = 10;

        // Wait for the WiFi event
        while (true) {
            switch (WiFi.status()) {
            case WL_NO_SSID_AVAIL:   log_debug("[WiFi] SSID not found"); break;
            case WL_CONNECTION_LOST: log_debug("[WiFi] Connection was lost"); break;
            case WL_SCAN_COMPLETED:  log_debug("[WiFi] Scan is completed"); break;
            case WL_DISCONNECTED:    log_debug("[WiFi] WiFi is disconnected"); break;
            case WL_CONNECT_FAILED:
                log_debug("[WiFi] Failed - WiFi not connected!");
                return false;
            case WL_CONNECTED:
                log_debug("[WiFi] WiFi is connected!  IP address: %s", WiFi.localIP().toString().c_str());
                m_serverIp = WiFi.localIP();
                // Ensure AP mode flag reflects current station connection
                m_isApMode = false;
                #if ESP_FS_WS_MDNS
                // Station connected: stop mDNS to save memory (kept only for AP verification)
                MDNS.end();
                #endif
                return true;
            default:
                log_debug("[WiFi] WiFi Status: %d", WiFi.status());
                break;
            }
            delay(tryDelay);
            if (numberOfTries <= 0) {
                log_debug("[WiFi] Failed to connect to WiFi!");
                WiFi.disconnect();  // Use disconnect function to force stop trying to connect
                // Keep AP flag unchanged here; caller may start AP later
                return false;
            }
            else {
                numberOfTries--;
            }
        }
    }
    return false;
}



bool AsyncFsWebServer::startCaptivePortal(const char* ssid, const char* pass, const char* redirectTargetURL) {
    // Start AP mode
    delay(100);
    WiFi.mode(WIFI_AP);
    
    // Configure IP address, gateway and netmask for AP mode
    IPAddress apIP(192, 168, 4, 1);
    IPAddress netmask(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netmask);
    
    if (!WiFi.softAP(ssid, pass)) {
        log_error("Captive portal failed to start: WiFi.softAP() failed!");
        return false;
    }
    m_serverIp = WiFi.softAPIP();
    m_isApMode = true;

    m_dnsServer = new DNSServer();
    m_dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    if (! m_dnsServer->start(53, "*", m_serverIp)) {
        log_error("Captive portal failed to start: no sockets for DNS server available!");
        return false;
    }
    m_captive = new CaptiveRequestHandler(redirectTargetURL);
    addHandler(m_captive).setFilter(ON_AP_FILTER); //only when requested from AP
    
    #if ESP_FS_WS_MDNS
    // Start MDNS only in AP mode for connection verification to the captive SSID
    if (!MDNS.begin(m_host.c_str())){
        log_error("MDNS responder not started");
    } else {
        log_debug("MDNS responder started %s (AP mode)", m_host.c_str());
        MDNS.addService("http", "tcp", m_port);
        MDNS.setInstanceName("async-fs-webserver");
    }
    #endif

    log_info("Captive portal started. Redirecting all requests to %s", redirectTargetURL);

    return true;
}


// edit page, in usefull in some situation, but if you need to provide only a web interface, you can disable
#if ESP_FS_WS_EDIT

void AsyncFsWebServer::handleFileEdit(AsyncWebServerRequest *request) {
    if (m_pageUser != nullptr) {
        if(!request->authenticate(m_pageUser, m_pagePswd))
            return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)_acedit_min_htm, sizeof(_acedit_min_htm));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

/*
    Return the list of files in the directory specified by the "dir" query string parameter.
    Also demonstrates the use of chunked responses.
*/
void AsyncFsWebServer::handleFileList(AsyncWebServerRequest *request)
{
    if (!request->hasArg("dir")) {
        return request->send(400, "DIR ARG MISSING");
    }

    String path = request->arg("dir");
    log_debug("handleFileList: %s", path.c_str());
    if (path != "/" && !m_filesystem->exists(path)) {
        return request->send(400, "BAD PATH");
    }

    File root = m_filesystem->open(path, "r");
    String output;
    output.reserve(512);
    output = "[";
    if (root.isDirectory()) {
        File file = root.openNextFile();
        bool first = true;
        while (file) {
            if (!first) output += ",";
            first = false;
            String filename;
            if (typeName.equals("SPIFFS")) {
                // SPIFFS returns full path and subfolders are unsupported, remove leading '/'                
                #if defined(ESP32)
                filename += file.path();
                #elif defined(ESP8266)
                filename += file.fullName();
                #endif
                filename.remove(0, 1);
            } 
            else {
                filename = file.name();
                if (filename.lastIndexOf("/") > -1) {
                    filename.remove(0, filename.lastIndexOf("/") + 1);
                }
            }         
            AsyncFSWebServer::Json item;
            item.setString("type", (file.isDirectory()) ? "dir" : "file");
            item.setNumber("size", file.size());
            item.setString("name", filename);
            output += item.serialize();
            file = root.openNextFile();
        }
    }
    output += "]";
    request->send(200, "text/json", output);
}

/*
    Handle the creation/rename of a new file
    Operation      | req.responseText
    ---------------+--------------------------------------------------------------
    Create file    | parent of created file
    Create folder  | parent of created folder
    Rename file    | parent of source file
    Move file      | parent of source file, or remaining ancestor
    Rename folder  | parent of source folder
    Move folder    | parent of source folder, or remaining ancestor
*/
void AsyncFsWebServer::handleFileCreate(AsyncWebServerRequest *request)
{
    String path = request->arg("path");
    if (path.isEmpty()) {
        return request->send(400, "PATH ARG MISSING");
    }
    if (path == "/") {
        return request->send(400, "BAD PATH");
    }

    String src = request->arg("src");
    if (src.isEmpty())  {
        // No source specified: creation
        log_debug("handleFileCreate: %s\n", path.c_str());
        if (path.endsWith("/")) {
            // Create a folder
            path.remove(path.length() - 1);
            if (!m_filesystem->mkdir(path)) {
                return request->send(500, "MKDIR FAILED");
            }
        }
        else  {
            // Create a file
            File file = m_filesystem->open(path, "w");
            if (file) {
                file.write(0);
                file.close();
            }
            else {
                return request->send(500, "CREATE FAILED");
            }
        }
        request->send(200,  path.c_str());
    }
    else  {
        // Source specified: rename
        if (src == "/") {
            return request->send(400, "BAD SRC");
        }
        if (!m_filesystem->exists(src)) {
            return request->send(400,  "FILE NOT FOUND");
        }

        log_debug("handleFileCreate: %s from %s\n", path.c_str(), src.c_str());
        if (path.endsWith("/")) {
            path.remove(path.length() - 1);
        }
        if (src.endsWith("/")) {
            src.remove(src.length() - 1);
        }
        if (!m_filesystem->rename(src, path))  {
            return request->send(500, "RENAME FAILED");
        }
        sendOK(request);
    }
}

/*
    Handle a file deletion request
    Operation      | req.responseText
    ---------------+--------------------------------------------------------------
    Delete file    | parent of deleted file, or remaining ancestor
    Delete folder  | parent of deleted folder, or remaining ancestor
*/

void AsyncFsWebServer::deleteContent(String& path) {
  File file = m_filesystem->open(path.c_str(), "r");
  if (!file.isDirectory()) {
    file.close();
    m_filesystem->remove(path.c_str());
    log_info("File %s deleted", path.c_str());
    return;
  }

  file.rewindDirectory();
  while (true) {
    File entry = file.openNextFile();
    if (!entry) {
      break;
    }
    String entryPath = path;
    entryPath += "/";
    entryPath += entry.name();
    if (entry.isDirectory()) {
      entry.close();
      deleteContent(entryPath);
    }
    else {
      entry.close();
      m_filesystem->remove(entryPath.c_str());
      log_info("File %s deleted", path.c_str());
    }
    yield();
  }
  m_filesystem->rmdir(path.c_str());
  log_info("Folder %s removed", path.c_str());
  file.close();
}



void AsyncFsWebServer::handleFileDelete(AsyncWebServerRequest *request) {
    String path = request->arg((size_t)0);
    if (path.isEmpty() || path == "/")  {
        return request->send(400, "BAD PATH");
    }
    if (!m_filesystem->exists(path))  {
        return request->send(400, "File Not Found");
    }
    deleteContent(path);
    sendOK(request);
}

/*
    Return the FS type, status and size info
*/
void AsyncFsWebServer::handleFsStatus(AsyncWebServerRequest *request)
{
    log_debug("handleStatus");
    fsInfo_t info = {1024, 1024, "ESP Filesystem"};
#ifdef ESP8266
    FSInfo fs_info;
    m_filesystem->info(fs_info);
    info.totalBytes = fs_info.totalBytes;
    info.usedBytes = fs_info.usedBytes;
#endif
    if (getFsInfo != nullptr) {
        getFsInfo(&info);
    }
    AsyncFSWebServer::Json doc;
    doc.setString("type", info.fsName);
    doc.setString("isOk", m_filesystem_ok ? "true" : "false");

    if (m_filesystem_ok)  {
        IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        doc.setString("totalBytes", String(info.totalBytes));
        doc.setString("usedBytes", String(info.usedBytes));
        doc.setString("mode", WiFi.status() == WL_CONNECTED ? "Station" : "Access Point");
        doc.setString("ssid", WiFi.SSID());
        doc.setString("ip", ip.toString());
    }
    doc.setString("unsupportedFiles", "");
    String json = doc.serialize();
    request->send(200, "application/json", json);
}
#endif // ESP_FS_WS_EDIT

