
#include "AsyncFsWebServer.h"


bool AsyncFsWebServer::init(AwsEventHandler wsHandle) {
    File file = m_filesystem->open(CONFIG_FOLDER, "r");
    if (!file) {
        DebugPrintln("Failed to open /setup directory. Create new folder\n");
        m_filesystem->mkdir(CONFIG_FOLDER);
        ESP.restart();
    }
    m_filesystem_ok = true;

    // Check if config file exist, and create if necessary
    file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, "r");
    if (!file) {
        file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, "w");
        file.print("{\"param-box0\": \"\"}");
        file.close();
    } else
        file.close();

    if (m_host != nullptr) {
        if (MDNS.begin(m_host)){
            DebugPrintln("MDNS responder started");
        }
    }
    //////////////////////    BUILT-IN HANDLERS    ////////////////////////////
    using namespace std::placeholders;

    on("/favicon.ico", HTTP_GET, std::bind(&AsyncFsWebServer::sendOK, this, _1));
    on("/connect", HTTP_POST, std::bind(&AsyncFsWebServer::doWifiConnection, this, _1));
    on("/scan", HTTP_GET, std::bind(&AsyncFsWebServer::handleScanNetworks, this, _1));
    on("/wifistatus", HTTP_GET, std::bind(&AsyncFsWebServer::getStatus, this, _1));
    on("/clear_config", HTTP_GET, std::bind(&AsyncFsWebServer::clearConfig, this, _1));
    on("/setup", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));
    on("*", HTTP_HEAD, std::bind(&AsyncFsWebServer::handleFileName, this, _1));

    on("/upload", HTTP_POST,
        std::bind(&AsyncFsWebServer::sendOK, this, _1),
        std::bind(&AsyncFsWebServer::handleUpload, this, _1, _2, _3, _4, _5, _6)
    );

    on("/update", HTTP_POST,
        std::bind(&AsyncFsWebServer::update_second, this, _1),
        std::bind(&AsyncFsWebServer::update_first, this, _1, _2, _3, _4, _5, _6)
    );

    on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "RESET");
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

    on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", CONFIG_FOLDER CONFIG_FILE);
    });

    onNotFound( std::bind(&AsyncFsWebServer::notFound, this, _1));
    serveStatic("/", *m_filesystem, "/").setDefaultFile("index.htm");

    if (wsHandle != nullptr)
        m_ws->onEvent(wsHandle);
    else
        m_ws->onEvent(std::bind(&AsyncFsWebServer::handleWebSocket,this, _1, _2, _3, _4, _5, _6));
    addHandler(m_ws);
    begin();
    MDNS.addService("http","tcp", m_port);
    MDNS.setInstanceName("async-fs-webserver");
    return true;
}


  void AsyncFsWebServer::enableFsCodeEditor() {
#ifdef INCLUDE_EDIT_HTM
    using namespace std::placeholders;
    on("/status", HTTP_GET, std::bind(&AsyncFsWebServer::handleFsStatus, this, _1));
    on("/list", HTTP_GET, std::bind(&AsyncFsWebServer::handleFileList, this, _1));
    on("/edit", HTTP_PUT, std::bind(&AsyncFsWebServer::handleFileCreate, this, _1));
    on("/edit", HTTP_DELETE, std::bind(&AsyncFsWebServer::handleFileDelete, this, _1));
    on("/edit", HTTP_POST,
        std::bind(&AsyncFsWebServer::sendOK, this, _1),
        std::bind(&AsyncFsWebServer::handleUpload, this, _1, _2, _3, _4, _5, _6)
    );
    on("/edit", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)_acedit_htm, sizeof(_acedit_htm));
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
#endif
  }


bool AsyncFsWebServer::captivePortal(AsyncWebServerRequest *request) {
    IPAddress ip = request->client()->localIP();
    char serverLoc[sizeof("https:://255.255.255.255/") + MAX_APNAME_LEN + 1];
    snprintf(serverLoc, sizeof(serverLoc), "http://%d.%d.%d.%d%s", ip[0], ip[1], ip[2], ip[3], m_apWebpage);

    // redirect if hostheader not server ip, prevent redirect loops
    String host = request->getHeader("Host")->toString();
    if (host.equals(serverLoc)) {
        AsyncWebServerResponse *response = request->beginResponse(302, F("text/html"), "");
        response->addHeader("Location", serverLoc);
        request->send(response);                 // Empty content inhibits Content-length header so we have to close the socket ourselves.
        request->client()->stop();               // Stop is needed because we sent no content length
        return true;
    }
    return false;
}


void AsyncFsWebServer::handleWebSocket(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len) {
   switch (type) {
        case WS_EVT_CONNECT:
            client->printf("{\"Websocket connected\": true, \"clients\": %u}", client->id());
            break;
        case WS_EVT_DISCONNECT:
            client->printf("{\"Websocket connected\": false, \"clients\": 0}");
            break;
        case WS_EVT_DATA: {
            AwsFrameInfo * info = (AwsFrameInfo*)arg;
            String msg = "";
            if(info->final && info->index == 0 && info->len == len){
                //the whole message is in a single frame and we got all of it's data
                Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
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

void AsyncFsWebServer::setTaskWdt(uint32_t timeout) {
    #if defined(ESP32)
    #if ESP_ARDUINO_VERSION_MAJOR > 2
    // esp_task_wdt_config_t twdt_config = {
    //     .timeout_ms = timeout,
    //     .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
    //     .trigger_panic = false,
    // };
    // ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
    #else
    ESP_ERROR_CHECK(esp_task_wdt_init(timeout/1000, 0));
    #endif
    #endif
}


void AsyncFsWebServer::handleSetup(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)SETUP_HTML, SETUP_HTML_SIZE);
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("X-Config-File", CONFIG_FOLDER CONFIG_FILE);
    request->send(response);
}

void AsyncFsWebServer::handleFileName(AsyncWebServerRequest *request) {
    if (m_filesystem->exists(request->url()))
        request->send(302, "text/plain", "OK");
    request->send(404, "text/plain", "File not found");
}


void AsyncFsWebServer::sendOK(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "OK");
}

void AsyncFsWebServer::notFound(AsyncWebServerRequest *request) {
    request->send(404);
    Serial.printf("Resource %s not found\n", request->url().c_str());
}

void AsyncFsWebServer::getStatus(AsyncWebServerRequest *request) {
    uint32_t ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
    String reply = "{\"firmware\": \"";
    reply += m_version;
    reply += "\", \"mode\":\"";
    reply += WiFi.status() == WL_CONNECTED ? "Station" : "Access Point";
    reply += "\", \"ip\":";
    reply += ip;
    reply += "}";
    request->send(200, "application/json", reply);
}

void AsyncFsWebServer::clearConfig(AsyncWebServerRequest *request) {
    if (m_filesystem->remove(CONFIG_FOLDER CONFIG_FILE))
        request->send(200, "text/plain", "Clear config OK");
    else
        request->send(200, "text/plain", "Clear config not done");
}

IPAddress AsyncFsWebServer::setAPmode(const char *ssid, const char *psk)  {
    using namespace std::placeholders;
    WiFi.mode(WIFI_AP);
    WiFi.persistent(false);
    WiFi.softAP(ssid, psk);

    // Captive Portal redirect
    on("/redirect", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));
    on("/connecttest.txt", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));
    on("/hotspot-detect.html", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));
    on("/generate_204", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));
    on("/gen_204", HTTP_GET, std::bind(&AsyncFsWebServer::handleSetup, this, _1));

    Serial.print(F("\nAP mode.\nServer IP address: "));
    Serial.println(WiFi.softAPIP());
    Serial.println();
    return WiFi.softAPIP();
}


  void AsyncFsWebServer::setLogoBase64(const char* logo, const char* width, const char* height, bool overwrite) {
      char filename[32] = {CONFIG_FOLDER};
      strcat(filename, "/img-logo-");
      strcat(filename, width);
      strcat(filename, "_");
      strcat(filename, height);
      strcat(filename, ".txt");
      optionToFile(filename, logo, overwrite);
      addOption("img-logo", filename);
    }

bool AsyncFsWebServer::optionToFile(const char* filename, const char* str, bool overWrite) {
    // Check if file is already saved
    File file = m_filesystem->open(filename, "r");
    if (file && !overWrite) {
        file.close();
        return true;
    }
    // Create or overwrite option file
    else {
        file = m_filesystem->open(filename, "w");
        if (file) {
            file.print(str);
            file.close();
            DebugPrintf("File %s saved\n", filename);
            return true;
        }
        else {
            DebugPrintf("Error writing file %s\n", filename);
        }
    }
    return false;
}

void AsyncFsWebServer::addSource(const char* source, const char* tag, bool overWrite) {
    String path = CONFIG_FOLDER;
    path += "/";
    path += tag;
    if (String(tag).indexOf("html")> -1)
        path += ".htm";
    else if (String(tag).indexOf("css") > -1)
        path += ".css";
    else if (String(tag).indexOf("javascript") > -1)
        path += ".js";
    optionToFile(path.c_str(), source, overWrite);
    addOption(tag, path.c_str(), false);
}

void AsyncFsWebServer::addHTML(const char* html, const char* id, bool overWrite) {
    String jsonId = "raw-html-" + String(id);
    addSource(html, jsonId.c_str(), overWrite);
}

void AsyncFsWebServer::addCSS(const char* css,  const char* id, bool overWrite) {
    String jsonId = "raw-css-" + String(id);
    addSource(css, jsonId.c_str(), overWrite);
}

void AsyncFsWebServer::addJavascript(const char* script,  const char* id, bool overWrite) {
    String jsonId = "raw-javascript-" + String(id);
    addSource(script, jsonId.c_str(), overWrite);
}


void AsyncFsWebServer::addDropdownList(const char *label, const char** array, size_t size) {
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
    else {
        DebugPrintln(F("File not found, will be created new configuration file"));
    }
    numOptions++ ;

    // If key is present in json, we don't need to create it.
    if (doc.containsKey(label))
        return;

    JsonObject obj = doc.createNestedObject(label);
    obj["selected"] = array[0];     // first element selected as default
    JsonArray arr = obj.createNestedArray("values");
    for (unsigned int i=0; i<size; i++) {
        arr.add(array[i]);
    }

    file = m_filesystem->open(CONFIG_FOLDER CONFIG_FILE, "w");
    if (serializeJsonPretty(doc, file) == 0) {
        DebugPrintln(F("Failed to write to file"));
    }
    file.close();
}

void AsyncFsWebServer::handleScanNetworks(AsyncWebServerRequest *request) {
    // Increase task WDT timeout
    setTaskWdt(15000);

    DebugPrint("Start scan WiFi networks");
    int res = WiFi.scanNetworks();
    DebugPrintf(" done!\nNumber of networks: %d\n", res);
    String json = "[";
    if (res > 0) {
        for (int i = 0; i < res; ++i) {
            if (i) json += ",";
            json += "{";
            json += "\"strength\":";
            json += WiFi.RSSI(i);
            json += ",\"ssid\":\"";
            json += WiFi.SSID(i);
            json += "\",\"security\":\"";
            #if defined(ESP8266)
            json += WiFi.encryptionType(i) == AUTH_OPEN ? "none" : "enabled";
            #elif defined(ESP32)
            json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "none" : "enabled";
            #endif
            json += "\"}";
        }
        WiFi.scanDelete();
    }
    json += "]";
    request->send(200, "application/json", json);
    DebugPrintln(json);
}

void AsyncFsWebServer::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {

    Serial.println("Handle upload POST");
    if (!index) {
        // Increase task WDT timeout
        setTaskWdt(15000);
        // open the file on first call and store the file handle in the request object
        Serial.printf("Client: %s %s\n", request->client()->remoteIP().toString().c_str(), request->url().c_str());
        request->_tempFile = m_filesystem->open(filename, "w");
        Serial.printf("Upload Start.\nWriting file %s\n", filename.c_str());
    }

    if (len) {
        // stream the incoming chunk to the opened file
        static int i = 0;
        request->_tempFile.write(data, len);
        if(!i++ % 50) Serial.print("\n");
        Serial.print(".");
    }

    if (final) {
        // Restore task WDT timeout
        setTaskWdt(8000);
        // close the file handle as the upload is now done
        request->_tempFile.close();
        Serial.printf("\nUpload complete: %s, size: %d \n", filename.c_str(), index + len);
    }
}

void AsyncFsWebServer::doWifiConnection(AsyncWebServerRequest *request) {
    String ssid, pass;

    if (request->hasArg("ssid"))
        ssid = request->arg("ssid");

    if (request->hasArg("password"))
        pass = request->arg("password");

    if (request->hasArg("persistent")) {
        if (request->arg("persistent").equals("false")) {
            WiFi.persistent(false);
            #if defined(ESP8266)
                struct station_config stationConf;
                wifi_station_get_config_default(&stationConf);
                // Clear previuos configuration
                memset(&stationConf, 0, sizeof(stationConf));
                wifi_station_set_config(&stationConf);
            #elif defined(ESP32)
                wifi_config_t stationConf;
                esp_wifi_get_config(WIFI_IF_STA, &stationConf);
                // Clear previuos configuration
                memset(&stationConf, 0, sizeof(stationConf));
                esp_wifi_set_config(WIFI_IF_STA, &stationConf);
            #endif
        }
        else {
            // Store current WiFi configuration in flash
            WiFi.persistent(true);
            #if defined(ESP8266)
                struct station_config stationConf;
                wifi_station_get_config_default(&stationConf);
                // Clear previuos configuration
                memset(&stationConf, 0, sizeof(stationConf));
                os_memcpy(&stationConf.ssid, ssid.c_str(), ssid.length());
                os_memcpy(&stationConf.password, pass.c_str(), pass.length());
                wifi_set_opmode(STATION_MODE);
                wifi_station_set_config(&stationConf);
            #elif defined(ESP32)
                wifi_config_t stationConf;
                esp_wifi_get_config(WIFI_IF_STA, &stationConf);
                // Clear previuos configuration
                memset(&stationConf, 0, sizeof(stationConf));
                memcpy(&stationConf.sta.ssid, ssid.c_str(), ssid.length());
                memcpy(&stationConf.sta.password, pass.c_str(), pass.length());
                esp_wifi_set_config(WIFI_IF_STA, &stationConf);
            #endif

        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        // IPAddress ip = WiFi.localIP();
        String resp = F("ESP is currently connected to a WiFi network.<br><br>"
        "Actual connection will be closed and a new attempt will be done with <b>");
        resp += ssid;
        resp += "</b> WiFi network.";
        request->send(200, "text/plain", resp);
        delay(250);
        Serial.println("\nDisconnect from current WiFi network");
        WiFi.disconnect();
    }

    // Connect to the provided SSID
    if (ssid.length() && pass.length()) {
        WiFi.mode(WIFI_AP_STA);
        Serial.printf("\n\n\nConnecting to %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());

        uint32_t beginTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print("*.*");
            #if defined(ESP8266)
            ESP.wdtFeed();
            #endif
            if (millis() - beginTime > 10000) {
                request->send(408, "text/plain", "<br><br>Connection timeout!<br>Check password or try to restart ESP.");
                delay(100);
                Serial.println("\nWiFi connect timeout!");
                return;
            }
        }
        // reply to client
        if (WiFi.status() == WL_CONNECTED) {
            // WiFi.softAPdisconnect();
            IPAddress ip = WiFi.localIP();
            Serial.print(F("\nConnected to Wifi! IP address: "));
            Serial.println(ip);
            String serverLoc = F("http://");
            for (int i = 0; i < 4; i++)
                serverLoc += i ? "." + String(ip[i]) : String(ip[i]);

            String resp = F("Restart ESP and then reload this page from <a href='");
            resp += serverLoc;
            resp += F("/setup'>the new LAN address</a>");
            request->send(200, "text/plain", resp);
        }
    }
    request->send(401, "text/plain", "Wrong credentials provided");
}

void AsyncFsWebServer::update_second(AsyncWebServerRequest *request) {
    // the request handler is triggered after the upload has finished...
    // create the response, add header, and send response
    String txt;
    if (Update.hasError()) {
        txt = "Error! Formware update not performed";
    }
    else {
        txt = "Update completed successfully. The ESP32 will restart";
    }
    AsyncWebServerResponse *response = request->beginResponse((Update.hasError())?500:200, "text/plain", txt);
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);

    if (!Update.hasError()) {
        delay(500);
        ESP.restart();
    }
}

void  AsyncFsWebServer::update_first(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {

    if (!m_contentLen) {
        int headers = request->headers();
        for(int i=0;i<headers;i++){
            AsyncWebHeader* h = request->getHeader(i);
            if (h->name().indexOf("Content-Length") > -1) {
                m_contentLen = h->value().toInt();
                DebugPrintln(h->value());
            }
        }
    }

    if (!index) {
        // Increase task WDT timeout
        setTaskWdt(15000);

        if(!request->hasParam("MD5", true)) {
            return request->send(400, "text/plain", "MD5 parameter missing");
        }

        if(!Update.setMD5(request->getParam("MD5", true)->value().c_str())) {
            return request->send(400, "text/plain", "MD5 parameter invalid");
        }

    #if defined(ESP8266)
        int cmd = (filename == "filesystem") ? U_FS : U_FLASH;
    #elif defined(ESP32)
        int cmd = (filename == "filesystem") ? U_SPIFFS : U_FLASH;
    #endif
        if (!Update.begin(0xFFFFFFFF, cmd)) {
            Update.printError(Serial);
            return request->send(400, "text/plain", "OTA could not begin");
        }
    }

    // Write chunked data to the free sketch space
    if (len){
        if (Update.write(data, len) != len) {
            return request->send(400, "text/plain", "OTA could not begin");
        }
        static uint32_t sendTime;
        if (millis() - sendTime > 1000) {
            sendTime = millis();
            char buffer[100];
            snprintf(buffer, sizeof(buffer),"Update... Get %d of %d bytes", index, m_contentLen);
            m_ws->textAll(buffer);
            DebugPrintln(buffer);
        }
    }

    if (final) { // if the final flag is set then this is the last frame of data
        if (!Update.end(true)) { //true to set the size to the current progress
            Update.printError(Serial);
            return request->send(400, "text/plain", "Could not end OTA");
        }
        m_ws->textAll("Update done! ESP will be restarted");
        DebugPrintln("Update done! ESP will be restarted");
        delay(100);
        // restore task WDT timeout
        setTaskWdt(8000);
    }
    return;
}


IPAddress AsyncFsWebServer::startWiFi(uint32_t timeout, const char *apSSID, const char *apPsw, CallbackF fn ) {
    IPAddress ip;
    m_timeout = timeout;
    WiFi.mode(WIFI_STA);
    const char *_ssid;
    const char *_pass;
#if defined(ESP8266)
    struct station_config conf;
    wifi_station_get_config_default(&conf);
    _ssid = reinterpret_cast<const char *>(conf.ssid);
    _pass = reinterpret_cast<const char *>(conf.password);
#elif defined(ESP32)
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    _ssid = reinterpret_cast<const char *>(conf.sta.ssid);
    _pass = reinterpret_cast<const char *>(conf.sta.password);
#endif

    if (strlen(_ssid) && strlen(_pass)) {
        WiFi.begin(_ssid, _pass);
        Serial.print(F("Connecting to "));
        Serial.println(_ssid);
        uint32_t startTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
            // execute callback function during wifi connection
            if (fn != nullptr)
                fn();

            delay(250);
            Serial.print(".");
            if (WiFi.status() == WL_CONNECTED) {
                ip = WiFi.localIP();
                return ip;
            }
            // If no connection after a while go in Access Point mode
            if (millis() - startTime > m_timeout) {
                Serial.println("Timeout!");
                break;
            }
        }
    }

    if (apSSID != nullptr && apPsw != nullptr)
        return setAPmode(apSSID, apPsw);
    else
        return setAPmode("ESP_AP", "123456789");

    ip = WiFi.softAPIP();
    return ip;
}


// edit page, in usefull in some situation, but if you need to provide only a web interface, you can disable
#ifdef INCLUDE_EDIT_HTM

/*
    Return the list of files in the directory specified by the "dir" query string parameter.
    Also demonstrates the use of chuncked responses.
*/
void AsyncFsWebServer::handleFileList(AsyncWebServerRequest *request)
{
    if (!request->hasArg("dir")) {
        return request->send(400, "DIR ARG MISSING");
    }

    String path = request->arg("dir");
    DebugPrintln("handleFileList: " + path);
    if (path != "/" && !m_filesystem->exists(path)) {
        return request->send(400, "BAD PATH");
    }

    File root = m_filesystem->open(path, "r");
    String output = "[";
    if (root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            String filename = file.name();
            if (filename.lastIndexOf("/") > -1) {
                filename.remove(0, filename.lastIndexOf("/") + 1);
            }
            if (output != "[") {
                output += ',';
            }
            output += "{\"type\":\"";
            output += (file.isDirectory()) ? "dir" : "file";
            output += "\",\"size\":\"";
            output += file.size();
            output += "\",\"name\":\"";
            output += filename;
            output += "\"}";
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
        DebugPrintf_P(PSTR("handleFileCreate: %s\n"), path.c_str());
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

        DebugPrintf_P(PSTR("handleFileCreate: %s from %s\n"), path.c_str(), src.c_str());
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
void AsyncFsWebServer::handleFileDelete(AsyncWebServerRequest *request) {

    String path = request->arg((size_t)0);
    if (path.isEmpty() || path == "/")  {
        return request->send(400, "BAD PATH");
    }

    DebugPrintf_P(PSTR("handleFileDelete: %s\n"), path.c_str());
    if (!m_filesystem->exists(path))  {
        return request->send(400, "File Not Found");
    }
    // deleteRecursive(path);
    File root = m_filesystem->open(path, "r");
    // If it's a plain file, delete it
    if (!root.isDirectory()) {
        root.close();
        m_filesystem->remove(path);
        sendOK(request);
    }
    else  {
        m_filesystem->rmdir(path);
         sendOK(request);
    }
}

/*
    Return the FS type, status and size info
*/
void AsyncFsWebServer::handleFsStatus(AsyncWebServerRequest *request)
{
    DebugPrintln(PSTR("handleStatus"));
    fsInfo_t info = {1024, 1024, "Unset"};
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
    json.reserve(128);
    json = "{\"type\":\"";
    json += info.fsName;
    json += "\", \"isOk\":";
    if (m_filesystem_ok)  {
        uint32_t ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        json += PSTR("\"true\", \"totalBytes\":\"");
        json += info.totalBytes;
        json += PSTR("\", \"usedBytes\":\"");
        json += info.usedBytes;
        json += PSTR("\", \"mode\":\"");
        json += WiFi.status() == WL_CONNECTED ? "Station" : "Access Point";
        json += PSTR("\", \"ssid\":\"");
        json += WiFi.SSID();
        json += PSTR("\", \"ip\":\"");
        json += ip;
        json += "\"";
    }
    else
        json += "\"false\"";
    json += PSTR(",\"unsupportedFiles\":\"\"}");
    request->send(200, "application/json", json);
}
#endif // INCLUDE_EDIT_HTM

