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

    // Handler for serving the isolated credentials script (Gzipped from PROGMEM)
    on("/creds.js", HTTP_GET, [this](AsyncWebServerRequest *request) {              
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)_accreds_js, sizeof(_accreds_js));
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("X-Config-File", ESP_FS_WS_CONFIG_FILE);
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });

    on("/setup", HTTP_GET, [this](AsyncWebServerRequest *request) { this->handleSetup(request); });
    
    // Serve default logo from PROGMEM when no custom logo exists on filesystem
    on("/config/logo.svg", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!m_filesystem->exists("/config/logo.svg") && 
            !m_filesystem->exists("/config/logo.svg.gz") &&
            !m_filesystem->exists("/config/logo.png") &&
            !m_filesystem->exists("/config/logo.jpg") &&
            !m_filesystem->exists("/config/logo.gif")) {
            AsyncWebServerResponse *response = request->beginResponse(200, "image/svg+xml", (uint8_t*)_aclogo_svg, sizeof(_aclogo_svg));
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "public, max-age=86400");
            request->send(response);
        } else {
            request->send(404);  // Let serveStatic handle it
        }
    });
    on("/connect", HTTP_POST, [this](AsyncWebServerRequest *request) { this->doWifiConnection(request); });
    on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) { this->handleScanNetworks(request); });
    on("/getStatus", HTTP_GET, [this](AsyncWebServerRequest *request) { this->getStatus(request); });
    on("/clear_config", HTTP_GET, [this](AsyncWebServerRequest *request) { this->clearConfig(request); });
    // Simple WiFi status endpoint
    on(AsyncURIMatcher::exact("/wifi"), HTTP_GET, [](AsyncWebServerRequest *request) {
        CJSON::Json doc;
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

    // WiFi credentials management (no plaintext passwords exposed)
    on(AsyncURIMatcher::exact("/wifi/credentials"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        CJSON::Json json_array;
        json_array.createArray();
        if (m_credentialManager) {
            std::vector<WiFiCredential>* creds = m_credentialManager->getCredentials();
            if (creds) {
                for (size_t i = 0; i < creds->size(); ++i) {
                    const WiFiCredential &c = (*creds)[i];
                    CJSON::Json item;
                    item.setNumber("index", static_cast<int>(i));
                    item.setString("ssid", c.ssid);

                    // Static network configuration (if present)
                    if (c.local_ip != IPAddress(0, 0, 0, 0)) {
                        item.setString("ip", c.local_ip.toString());
                        item.setString("gateway", c.gateway.toString());
                        item.setString("subnet", c.subnet.toString());

                        // Optional per-SSID DNS configuration
                        if (c.dns1 != IPAddress(0, 0, 0, 0)) {
                            item.setString("dns1", c.dns1.toString());
                        }
                        if (c.dns2 != IPAddress(0, 0, 0, 0)) {
                            item.setString("dns2", c.dns2.toString());
                        }
                    }

                    item.setNumber("hasPassword", c.pwd_len > 0 ? 1 : 0);
                    json_array.add(item);
                }
            }
        }
        request->send(200, "application/json", json_array.serialize());
    });
    // Delete a single credential (by index) or clear all if no index is provided
    on("/wifi/credentials", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!m_credentialManager) {
            request->send(404, "text/plain", "Credential manager not available");
            return;
        }
        bool ok = false;
        if (request->hasArg("index")) {
            int idx = request->arg("index").toInt();
            ok = m_credentialManager->removeCredential(static_cast<uint8_t>(idx));
        } else {
            m_credentialManager->clearAll();
            ok = true;
        }
#if defined(ESP32)
        m_credentialManager->saveToNVS();
#elif defined(ESP8266)
        m_credentialManager->saveToFS();
#endif
        request->send(ok ? 200 : 400, "text/plain", ok ? "OK" : "Invalid index");
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
void AsyncFsWebServer::enableFsCodeEditor() {
    on("/status", HTTP_GET, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFsStatus(request); });
    on("/list", HTTP_GET, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileList(request); });
    on("/edit", HTTP_PUT, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileCreate(request); });
    on("/edit", HTTP_DELETE, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileDelete(request); });
    on("/edit", HTTP_GET, (ArRequestHandlerFunction)[this](AsyncWebServerRequest *request) { handleFileEdit(request); });
    on("/edit", HTTP_POST,
        [this](AsyncWebServerRequest *request) { sendOK(request); },
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) { handleUpload(request, filename, index, data, len, final); }
    );
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

    // Check if authentication for all routes is turned on, and credentials are present:
    if (m_authAll && m_pageUser != nullptr) {
        if(!request->authenticate(m_pageUser, m_pagePswd))
            return request->requestAuthentication();
    }

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
    CJSON::Json doc;
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
    
    // Add logo and page title for immediate availability (avoid layout shift during load)
    String logoPath = "";
    String pageTitle = "";
    if (getSetupConfigurator()->getOptionValue("img-logo", logoPath) && logoPath.length() > 0) {
        doc.setString("img-logo", logoPath);
    }
    if (getSetupConfigurator()->getOptionValue("page-title", pageTitle) && pageTitle.length() > 0) {
        doc.setString("page-title", pageTitle);
    }
    
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
    WiFiScanResult scan = WiFiService::scanNetworks();
    request->send(200, "application/json", scan.json);
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
    // Use WiFiConnectParams as the main container for all connection options
    WiFiConnectParams params = WiFiConnectParams();
    String requestedHost;   // Optional hostname provided by the setup UI

    // Optional static IP configuration
    if (request->hasArg("ip_address") && request->hasArg("subnet") && request->hasArg("gateway")) {
        params.creds.gateway.fromString(request->arg("gateway"));
        params.creds.subnet.fromString(request->arg("subnet"));
        params.creds.local_ip.fromString(request->arg("ip_address"));
        params.dhcp = false;
        log_debug("Static IP requested: %s, GW: %s, SN: %s",
                 params.creds.local_ip.toString().c_str(),
                 params.creds.gateway.toString().c_str(),
                 params.creds.subnet.toString().c_str());
        log_debug("params.dhcp set to: %d", params.dhcp);
    }

    // Optional per-SSID DNS configuration (if provided by client)
    if (request->hasArg("dns1")) 
        params.creds.dns1.fromString(request->arg("dns1"));

    if (request->hasArg("dns2")) 
        params.creds.dns2.fromString(request->arg("dns2"));

    // Optional hostname override (for mDNS / shared hostname, max 32 chars)
    if (request->hasArg("hostname")) {
        requestedHost = request->arg("hostname");
        requestedHost.trim();
        if (requestedHost.length() > 32) {
            requestedHost.remove(32); // limit to 32 chars
        }
        if (requestedHost.length()) {
            m_host = requestedHost;
            if (m_credentialManager) {
                m_credentialManager->setHostname(m_host.c_str());
            }
        }
    }

    // Basic WiFi credentials
    if (request->hasArg("ssid")) {
        String ssid = request->arg("ssid");
        strncpy(params.creds.ssid, ssid.c_str(), sizeof(params.creds.ssid) - 1);
        params.creds.ssid[sizeof(params.creds.ssid) - 1] = '\0';
    }
    if (request->hasArg("password"))
        params.password = request->arg("password");

    // If no password provided but a stored credential exists for this SSID,
    // reuse the stored password without exposing it to the client.
    if (params.password.length() == 0 && strlen(params.creds.ssid) && m_credentialManager &&
        m_credentialManager->checkSSIDExists(params.creds.ssid)) {
        String stored = m_credentialManager->getPassword(params.creds.ssid);
        if (stored.length()) {
            params.password = stored;
        }
    }

    // Track if we had a working STA connection before this /connect
    // (used to decide fallbacks on failure).
    bool wasStaConnected = (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP);

    // By default, new credentials will be persisted to survive reboots,
    // but the client can disable this if they want a temporary connection.
    bool hasPersistentArg = request->hasArg("persistent");
    bool persistent = hasPersistentArg ? request->arg("persistent").equals("true") : true;
    WiFi.persistent(hasPersistentArg ? persistent : true);  // default true

    // Remember if this /connect was requested while we are serving the captive portal (AP mode).
    params.fromApClient = m_isApMode;
    params.host = m_host;
    params.timeout = m_timeout;
    params.wdtLongTimeout = AWS_LONG_WDT_TIMEOUT;
    params.wdtTimeout = AWS_WDT_TIMEOUT;

    // In AP/captive-portal mode we can still safely perform the
    // connection synchronously and return the detailed HTML result, because the AP interface remains up.
    if (params.fromApClient) {
        WiFiConnectResult result = WiFiService::connectWithParams(params);

        if (result.connected && persistent && strlen(params.creds.ssid) && params.password.length() && m_credentialManager) {
            // Credential is already populated in params.credential, just need to configure it based on dhcp flag
            if (params.dhcp) {
                // Clear static IP config for DHCP mode
                params.creds.local_ip = IPAddress(0, 0, 0, 0);
                params.creds.gateway = IPAddress(0, 0, 0, 0);
                params.creds.subnet = IPAddress(0, 0, 0, 0);
                params.creds.dns1 = IPAddress(0, 0, 0, 0);
                params.creds.dns2 = IPAddress(0, 0, 0, 0);
            }
            // Static IP config already set in params.creds if dhcp == false
            if (!m_credentialManager->updateCredential(params.creds, params.password.c_str())) {
                m_credentialManager->addCredential(params.creds, params.password.c_str());
            }
        #if defined(ESP32)
            m_credentialManager->saveToNVS();
        #elif defined(ESP8266)
            m_credentialManager->saveToFS();
        #endif
        }

        if (result.connected) {
            m_serverIp = result.ip;
        }

        log_debug("WiFi connect result body: %s", result.body.c_str());
        const char* contentType = (result.status == 200) ? "text/html" : "text/plain";
        request->send(result.status, contentType, result.body);
        return;
    }

    // STA mode: if already connected, always ask for confirmation before reconfiguring
    bool userConfirmed = request->hasArg("confirmed") && request->arg("confirmed").equals("true");
    
    if (!userConfirmed && WiFi.status() == WL_CONNECTED) {
        String resp;
        resp.reserve(512);
        resp = "<div id='action-confirm-sta-change'></div>";
        resp += "You are about to apply new WiFi configuration for <b>";
        resp += String(params.creds.ssid);
        resp += "</b>.<br><br><i>Note: This page may lose connection during the reconfiguration.</i>";
        resp += "<br><br>Do you want to proceed?";
        request->send(200, "text/html", resp);
        return;
    }

    // User confirmed: proceed with connection, 
    // but first set up the disconnect handler to attempt connection after response is sent (to avoid HTTP timeouts)
    
    // Attempt connection AFTER sending response
    request->onDisconnect([this, request, params, wasStaConnected]() mutable {        
        WiFiConnectResult result = WiFiService::connectWithParams(params);
        log_debug("Connection result: connected=%d", result.connected);

        // Save credentials ONLY if connection succeeded
        if (result.connected && strlen(params.creds.ssid) && params.password.length() && m_credentialManager) {
            if (params.dhcp) {
                // Clear static IP config for DHCP mode
                params.creds.local_ip = IPAddress(0, 0, 0, 0);
                params.creds.gateway = IPAddress(0, 0, 0, 0);
                params.creds.subnet = IPAddress(0, 0, 0, 0);
                params.creds.dns1 = IPAddress(0, 0, 0, 0);
                params.creds.dns2 = IPAddress(0, 0, 0, 0);
                log_debug("Saving DHCP config");
            } else {
                log_debug("Saving static IP: IP=%s, GW=%s, SN=%s, DNS1=%s, DNS2=%s",
                         params.creds.local_ip.toString().c_str(), 
                         params.creds.gateway.toString().c_str(),
                         params.creds.subnet.toString().c_str(),
                         params.creds.dns1.toString().c_str(),
                         params.creds.dns2.toString().c_str());
            }

            if (!m_credentialManager->updateCredential(params.creds, params.password.c_str())) {
                m_credentialManager->addCredential(params.creds, params.password.c_str());
            }
        #if defined(ESP32)
            m_credentialManager->saveToNVS();
        #elif defined(ESP8266)
            m_credentialManager->saveToFS();
        #endif
        }

        if (result.connected) {
            m_serverIp = result.ip;
        }
        else if (wasStaConnected && !params.fromApClient && strlen(m_apSSID) > 0) {
            log_info("WiFi connect failed, starting AP mode: SSID=%s", m_apSSID);
            startCaptivePortal(m_apSSID, m_apPassword, "/setup");
        }
    });

    // Send response FIRST to avoid HTTP timeout
    String resp;
    resp.reserve(512);
    resp = "<div id='action-async-applying'></div>";
    resp += "WiFi configuration applying for <b>";
    resp += String(params.creds.ssid);
    resp += "</b>.<br><br>";
    resp += "<i>The ESP is reconnecting...<br>Please reload this page after a few seconds.</i>";
    request->send(200, "text/html", resp);
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

bool AsyncFsWebServer::startMDNSResponder() {
#if ESP_FS_WS_MDNS
    return WiFiService::startMDNSResponder(m_dnsServer, m_host, m_port, m_serverIp);
#else
    return true;
#endif
}

bool AsyncFsWebServer::startWiFi(uint32_t timeout, CallbackF fn) {    
#ifdef ESP32
    if (m_credentialManager && m_credentialManager->loadFromNVS()) {
        log_debug("Credentials loaded from NVS");
        log_debug("%s", m_credentialManager->getDebugInfo().c_str());
    }
#else
    if (m_credentialManager && m_credentialManager->loadFromFS()) {
        log_debug("Credentials loaded from filesystem");
        log_debug("%s", m_credentialManager->getDebugInfo().c_str());
    }
#endif

    // If a shared hostname was stored in CredentialManager, prefer it
    if (m_credentialManager) {
        String storedHost = m_credentialManager->getHostname();
        if (storedHost.length()) {
            m_host = storedHost;
        }
    }

    WiFiStartResult result = WiFiService::startWiFi(m_credentialManager, m_filesystem, ESP_FS_WS_CONFIG_FILE, timeout);
    if (result.action == WiFiStartAction::Connected) {
        m_serverIp = result.ip;
        m_isApMode = false;
    #if ESP_FS_WS_MDNS
        WiFiService::startMDNSOnly(m_host, m_port);
        log_info("mDNS started on http://%s.local", m_host.c_str());
    #endif
        return true;
    }

    if (result.action == WiFiStartAction::StartAp && strlen(m_apSSID) > 0) {
        log_info("Starting AP mode: SSID=%s", m_apSSID);
        startCaptivePortal(m_apSSID, m_apPassword, "/setup");
        return true;
    }
    return false;
}


bool AsyncFsWebServer::startCaptivePortal(const char* ssid, const char* pass, const char* redirectTargetURL) {
    WiFiConnectParams params;
    strncpy(params.creds.ssid, ssid, sizeof(params.creds.ssid) - 1);
    params.creds.ssid[sizeof(params.creds.ssid) - 1] = '\0';
    params.password = pass;     
    return startCaptivePortal(params, redirectTargetURL);
}


bool AsyncFsWebServer::startCaptivePortal(WiFiConnectParams& params, const char *redirectTargetURL) {
    // Start AP mode
    if (!WiFiService::startAccessPoint(params, m_serverIp)) {
        return false;
    }
    m_isApMode = true;
    // Start DNS server
    this->startMDNSResponder();
    // Captive portal server to redirect all requests to the AP IP
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
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)_acedit_htm, sizeof(_acedit_htm));
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
            CJSON::Json item;
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
    CJSON::Json doc;
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

