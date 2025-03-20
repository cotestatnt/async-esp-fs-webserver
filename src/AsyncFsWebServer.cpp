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
#if ESP_FS_WS_SETUP_HTM
    File file = m_filesystem->open(ESP_FS_WS_CONFIG_FOLDER, "r");
    if (!file) {
        log_error("Failed to open /setup directory. Create new folder\n");
        m_filesystem->mkdir(ESP_FS_WS_CONFIG_FOLDER);
        ESP.restart();
    }
    m_filesystem_ok = true;

    // Check if config file exist, and create if necessary
    file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "r");
    if (!file) {
        file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "w");
        file.print("{\"wifi-box\": \"\",\n\t\"dhcp\": false}");
        file.close();
    } else
        file.close();
#endif

    //////////////////////    BUILT-IN HANDLERS    ////////////////////////////
    using namespace std::placeholders;

    //on("/favicon.ico", HTTP_GET, std::bind(&AsyncFsWebServer::sendOK, this, _1));
    on("/connect", HTTP_POST, std::bind(&AsyncFsWebServer::doWifiConnection, this, _1));
    on("/scan", HTTP_GET, std::bind(&AsyncFsWebServer::handleScanNetworks, this, _1));
    on("/getStatus", HTTP_GET, std::bind(&AsyncFsWebServer::getStatus, this, _1));
    on("/clear_config", HTTP_GET, std::bind(&AsyncFsWebServer::clearConfig, this, _1));
#if ESP_FS_WS_SETUP_HTM
    on("/setup", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));
#endif
    on("*", HTTP_HEAD, std::bind(&AsyncFsWebServer::handleFileName, this, _1));

    on("/upload", HTTP_POST,
        std::bind(&AsyncFsWebServer::sendOK, this, _1),
        std::bind(&AsyncFsWebServer::handleUpload, this, _1, _2, _3, _4, _5, _6)
    );

    on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", WiFi.localIP().toString());
        delay(500);
        ESP.restart();
    });

    on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        String reply = "{\"ssid\":\"";
        reply += WiFi.SSID();
        reply += "\", \"rssi\":";
        reply += WiFi.RSSI();
        reply += "}";
        request->send(200, "application/json", reply);
    });

    onUpdate();
    onNotFound( std::bind(&AsyncFsWebServer::notFound, this, _1));
    serveStatic("/", *m_filesystem, "/").setDefaultFile("index.htm");

    if (wsHandle != nullptr)
        m_ws->onEvent(wsHandle);
    else
        m_ws->onEvent(std::bind(&AsyncFsWebServer::handleWebSocket,this, _1, _2, _3, _4, _5, _6));
    addHandler(m_ws);
    begin();

    // Configure and start MDNS responder
    if (!MDNS.begin(m_host.c_str())){
        log_error("MDNS responder not started");
    } else {
        log_debug("MDNS responder started %s", m_host.c_str());
        MDNS.addService("http", "tcp", m_port);
        MDNS.setInstanceName("async-fs-webserver");
    }

    return true;
}


void AsyncFsWebServer::printFileList(fs::FS &fs, const char * dirname, uint8_t levels) {
    Serial.printf("\nListing directory: %s\n", dirname);
    File root = fs.open(dirname, "r");
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
        if (levels) {
            #ifdef ESP32
            printFileList(fs, file.path(), levels - 1);
            #elif defined(ESP8266)
            printFileList(fs, file.fullName(), levels - 1);
            #endif
        }
        } else {
        Serial.printf("|__ FILE: %s (%d bytes)\n",file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

void AsyncFsWebServer::enableFsCodeEditor() {
#if ESP_FS_WS_EDIT
    using namespace std::placeholders;
    on("/status", HTTP_GET, std::bind(&AsyncFsWebServer::handleFsStatus, this, _1));
    on("/list", HTTP_GET, std::bind(&AsyncFsWebServer::handleFileList, this, _1));
    on("/edit", HTTP_PUT, std::bind(&AsyncFsWebServer::handleFileCreate, this, _1));
    on("/edit", HTTP_DELETE, std::bind(&AsyncFsWebServer::handleFileDelete, this, _1));
    on("/edit", HTTP_GET, std::bind(&AsyncFsWebServer::handleFileEdit, this, _1));
    on("/edit", HTTP_POST,
        std::bind(&AsyncFsWebServer::sendOK, this, _1),
        std::bind(&AsyncFsWebServer::handleUpload, this, _1, _2, _3, _4, _5, _6)
    );
#endif
  }
void AsyncFsWebServer::handleWebSocket(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len) {
   switch (type) {
        case WS_EVT_CONNECT:
            client->printf("{\"Websocket connected\": true, \"clients\": %" PRIu32 "}", client->id());
            break;
        case WS_EVT_DISCONNECT:
            client->printf("{\"Websocket connected\": false, \"clients\": 0}");
            break;
        case WS_EVT_DATA: {
            AwsFrameInfo * info = (AwsFrameInfo*)arg;
            String msg = "";
            if(info->final && info->index == 0 && info->len == len){
                //the whole message is in a single frame and we got all of it's data
                Serial.printf("ws[%s][%" PRIu32 "] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
                if (info->opcode == WS_TEXT){
                    for(size_t i=0; i < info->len; i++) {
                        msg += (char) data[i];
                    }
                }
                else {
                    char buff[4];
                    for(size_t i=0; i < info->len; i++) {
                        sprintf(buff, "%02x ", (uint8_t) data[i]);
                        msg += buff ;
                    }
                }
                Serial.printf("%s\n",msg.c_str());
            }
        }
        default:
            break;
    }
}


void AsyncFsWebServer::setAuthentication(const char* user, const char* pswd) {
    m_pageUser = (char*) malloc(strlen(user)*sizeof(char));
    m_pagePswd = (char*) malloc(strlen(pswd)*sizeof(char));
    strcpy(m_pageUser, user);
    strcpy(m_pagePswd, pswd);
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

void AsyncFsWebServer::handleFileName(AsyncWebServerRequest *request) {
    if (m_filesystem->exists(request->url()))
        request->send(301, "text/plain", "OK");
    request->send(404, "text/plain", "File not found");
}

void AsyncFsWebServer::sendOK(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "OK");
}

void AsyncFsWebServer::notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
    log_debug("Resource %s not found\n", request->url().c_str());
}

void AsyncFsWebServer::getStatus(AsyncWebServerRequest *request) {
    JSON_DOC(256);
    doc["firmware"] = m_version;
    doc["mode"] =  WiFi.status() == WL_CONNECTED ? ("Station (" + WiFi.SSID()) +')' : "Access Point";
    doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["hostname"] = m_host;
#if ESP_FS_WS_SETUP
    doc["path"] = String(ESP_FS_WS_CONFIG_FILE).substring(1);   // remove first '/'
#endif
    doc["liburl"] = LIB_URL;
    String reply;
    serializeJson(doc, reply);
    request->send(200, "application/json", reply);
}


void AsyncFsWebServer::clearConfig(AsyncWebServerRequest *request) {
#if ESP_FS_WS_SETUP
    if (m_filesystem->remove(ESP_FS_WS_CONFIG_FILE))
        request->send(200, "text/plain", "Clear config OK");
    else
        request->send(200, "text/plain", "Clear config not done");
#endif
}


void AsyncFsWebServer::handleScanNetworks(AsyncWebServerRequest *request) {
    log_info("Start scan WiFi networks");
    int res = WiFi.scanComplete();

    if (res == -2){
        WiFi.scanNetworks(true);
    }
    else if (res) {
        log_info(" done!\nNumber of networks: %d", res);
        JSON_DOC(res*96);
        JsonArray array = doc.to<JsonArray>();
        for (int i = 0; i < res; ++i) {
            #if ARDUINOJSON_VERSION_MAJOR > 6
                JsonObject obj = array.add<JsonObject>();
            #else
                JsonObject obj = array.createNestedObject();
            #endif
            obj["strength"] = WiFi.RSSI(i);
            obj["ssid"] = WiFi.SSID(i);
            #if defined(ESP8266)
            obj["security"] = AUTH_OPEN ? "none" : "enabled";
            #elif defined(ESP32)
            obj["security"] = WIFI_AUTH_OPEN ? "none" : "enabled";
            #endif
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
        log_debug("%s", json.c_str());

        WiFi.scanDelete();
        if(WiFi.scanComplete() == -2){
            WiFi.scanNetworks(true);
        }

        return;
    }

    // The very first request will be empty, reload /scan endpoint
    request->send(200, "application/json", "{\"reload\" : 1}");
}


bool AsyncFsWebServer::createDirFromPath(const String& path) {
    String dir;
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
        int len = filename.length();
        char path[len+1];
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
        log_debug("Upload complete: %s, size: %d", filename.c_str(), index + len);
    }
}

void AsyncFsWebServer::doWifiConnection(AsyncWebServerRequest *request) {
    String ssid, pass;
    IPAddress gateway, subnet, local_ip;
    bool config = false,  newSSID = false;
    char resp[512] = {0};

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
        snprintf(resp, sizeof(resp),
            "ESP is already connected to <b>%s</b> WiFi!<br>"
            "Do you want close this connection and attempt to connect to <b>%s</b>?"
            "<br><br><i>Note:<br>Flash stored WiFi credentials will be updated.<br>"
            "The ESP will no longer be reachable from this web page "
            "due to the change of WiFi network.<br>To find out the new IP address, "
            "check the serial monitor or your router.<br></i>",
            WiFi.SSID().c_str(), ssid.c_str()
        );
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

        Serial.printf("\n\n\nConnecting to %s\n", ssid.c_str());
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
            for (int i = 0; i < 4; i++)
                serverLoc += i ? "." + String(m_serverIp[i]) : String(m_serverIp[i]);
            serverLoc += "/setup";
            snprintf(resp, sizeof(resp),
                "ESP successfully connected to %s WiFi network. <br><b>Restart ESP now?</b>"
                "<br><br><i>Note: disconnect your browser from ESP AP and then reload "
                "<a href='%s'>%s</a> or <a href='http://%s.local'>http://%s.local</a></i>",
                ssid.c_str(), serverLoc.c_str(), serverLoc.c_str(), m_host.c_str(), m_host.c_str()
            );

            log_debug("%s", resp);
            request->send(200, "application/json", resp);
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

        String txt = Update.hasError() ?  Update.errorString() : "Update Success<br>Restart ESP to load new firmware!\n";
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



bool AsyncFsWebServer::startWiFi(uint32_t timeout, CallbackF fn) {
    // Check if we need to config wifi connection
    IPAddress local_ip, subnet, gateway;

#if ESP_FS_WS_SETUP
    File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "r");
    JSON_DOC( max((int)(file.size() * 1.33), 2048));

    if (file) {
        // If file is present, load actual configuration
        DeserializationError error = deserializeJson(doc, file);
        if (error) {
            log_error("Failed to deserialize file, may be corrupted\n %s\n", error.c_str());
            file.close();
        }
        file.close();
        if (doc["dhcp"].as<bool>() == true) {
            gateway.fromString(doc["gateway"].as<String>());
            subnet.fromString(doc["subnet"].as<String>());
            local_ip.fromString(doc["ip_address"].as<String>());
            log_info("Manual config WiFi connection with IP: %s\n", local_ip.toString().c_str());
            if (!WiFi.config(local_ip, gateway, subnet)) {
                log_error("STA Failed to configure");
            }
            delay(100);
        }
    }
    else {
        log_error("File not found, will be created new configuration file");
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
                return true;
            default:
                log_debug("[WiFi] WiFi Status: %d", WiFi.status());
                break;
            }
            delay(tryDelay);
            if (numberOfTries <= 0) {
                log_debug("[WiFi] Failed to connect to WiFi!");
                WiFi.disconnect();  // Use disconnect function to force stop trying to connect
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
    if (!WiFi.softAP(ssid, pass)) {
        log_error("Captive portal failed to start: WiFi.softAP() failed!");
        return false;
    }
    m_serverIp = WiFi.softAPIP();

    m_dnsServer = new DNSServer();
    if (! m_dnsServer->start(53, "*", WiFi.softAPIP())) {
        log_error("Captive portal failed to start: no sockets for DNS server available!");
        return false;
    }
    m_captive = new CaptiveRequestHandler(redirectTargetURL);
    addHandler(m_captive).setFilter(ON_AP_FILTER); //only when requested from AP
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
    JSON_DOC(1024);
    JsonArray array = doc.to<JsonArray>();
    if (root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            #if ARDUINOJSON_VERSION_MAJOR > 6
            JsonObject obj = array.add<JsonObject>();
            #else
            JsonObject obj = array.createNestedObject();
            #endif
            String filename = file.name();
            if (filename.lastIndexOf("/") > -1) {
                filename.remove(0, filename.lastIndexOf("/") + 1);
            }
            obj["type"] = (file.isDirectory()) ? "dir" : "file";
            obj["size"] = file.size();
            obj["name"] = filename;
            file = root.openNextFile();
        }
    }
    String output;
    serializeJson(doc, output);
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
    String entryPath = path + "/" + entry.name();
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
    String json;
    json = "{\"type\":\"";
    json += info.fsName;
    json += "\", \"isOk\":";
    if (m_filesystem_ok)  {
        IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        json += PSTR("\"true\", \"totalBytes\":\"");
        json += info.totalBytes;
        json += PSTR("\", \"usedBytes\":\"");
        json += info.usedBytes;
        json += PSTR("\", \"mode\":\"");
        json += WiFi.status() == WL_CONNECTED ? "Station" : "Access Point";
        json += PSTR("\", \"ssid\":\"");
        json += WiFi.SSID();
        json += PSTR("\", \"ip\":\"");
        json += ip.toString();
        json += "\"";
    }
    else
        json += "\"false\"";
    json += PSTR(",\"unsupportedFiles\":\"\"}");
    request->send(200, "application/json", json);
}
#endif // ESP_FS_WS_EDIT

