#include "AsyncFsWebServer.h"

#if defined(ESP8266)
#include <Schedule.h>
#endif

namespace {
String serializeJsonDocument(cJSON *root) {
    if (!root) {
        return "{}";
    }

    char *raw = cJSON_PrintUnformatted(root);
    String out = raw ? String(raw) : String("{}");
    if (raw) {
        free(raw);
    }
    cJSON_Delete(root);
    return out;
}

void dispatchSetupJob(CallbackF job) {
#if defined(ESP8266)
    schedule_function(std::move(job));
#elif defined(ESP32)
    auto *callback = new CallbackF(std::move(job));
    if (xTaskCreate(
            [](void *ctx) {
                auto *cb = static_cast<CallbackF *>(ctx);
                (*cb)();
                delete cb;
                vTaskDelete(nullptr);
            },
            "aws-setup",
            6144,
            callback,
            1,
            nullptr) != pdPASS) {
        (*callback)();
        delete callback;
    }
#endif
}

struct SetupWsMessageBuffer {
    char *data = nullptr;
    size_t capacity = 0;

    ~SetupWsMessageBuffer() {
        free(data);
    }
};
}


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
    ConfigUpgrader upgrader(m_filesystem, ESP_FS_WS_CONFIG_FILE);
    bool migratedLegacySetupStorage = false;
    upgrader.migrateLegacySetupStorage("/config/config.json", "/config", ESP_FS_WS_CONFIG_FOLDER, &migratedLegacySetupStorage);
    if (migratedLegacySetupStorage) {
        ESP.restart();
        return false;
    }

    m_filesystem_ok = getSetupConfigurator()->checkConfigFile();
    if (getSetupConfigurator()->isOpened()) {
        log_debug("Config file %s closed", ESP_FS_WS_CONFIG_FILE);
        getSetupConfigurator()->closeConfiguration();
    }
    onUpdate();

        // Register /setup-ws before the setup page is first served to avoid a
        // first-load race on slower devices.
        initSetupWebSocket();

    on("/setup", HTTP_GET, [this](AsyncWebServerRequest *request) { this->handleSetup(request); });
    
    // Serve default logo from PROGMEM when no custom logo exists on filesystem
    on(ESP_FS_WS_CONFIG_FOLDER "/logo.svg", HTTP_GET, [this](AsyncWebServerRequest *request) {
        const String logoBase = String(ESP_FS_WS_CONFIG_FOLDER) + "/logo";
        if (!m_filesystem->exists(logoBase + ".svg") && 
            !m_filesystem->exists(logoBase + ".svg.gz") &&
            !m_filesystem->exists(logoBase + ".png") &&
            !m_filesystem->exists(logoBase + ".jpg") &&
            !m_filesystem->exists(logoBase + ".gif")) {
            AsyncWebServerResponse *response = request->beginResponse(200, "image/svg+xml", (uint8_t*)_aclogo_svg, sizeof(_aclogo_svg));
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "public, max-age=86400");
            request->send(response);
        } else {
            request->send(404);  // Let serveStatic handle it
        }
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
    
    on("/", HTTP_GET, [this](AsyncWebServerRequest *request) { this->handleIndex(request); });
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

#if ESP_FS_WS_SETUP
void AsyncFsWebServer::initSetupWebSocket() {
    if (m_setupWs) {
        return;
    }

    m_setupWs = new AsyncWebSocket("/setup-ws");
    m_setupWs->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->handleSetupWebSocket(server, client, type, arg, data, len);
    });
    addHandler(m_setupWs);
}

void AsyncFsWebServer::releaseSetupWebSocketIfIdle() {
    if (!m_setupWs) {
        return;
    }

    m_setupWs->cleanupClients();
}

void AsyncFsWebServer::handleSetupWebSocket(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            sendSetupWsEvent(client, "setup.socket.ready");
            break;
        case WS_EVT_DATA: {
            AwsFrameInfo *info = reinterpret_cast<AwsFrameInfo *>(arg);
            if (!info || info->opcode != WS_TEXT) {
                return;
            }

            if (info->index == 0 && info->final && len == info->len) {
                handleSetupWebSocketMessage(client, data, len);
                return;
            }

            if (info->index == 0) {
                delete static_cast<SetupWsMessageBuffer *>(client->_tempObject);
                auto *buffer = new SetupWsMessageBuffer();
                buffer->capacity = info->len;
                buffer->data = static_cast<char *>(malloc(info->len + 1));
                if (!buffer->data) {
                    delete buffer;
                    client->_tempObject = nullptr;
                    return;
                }
                buffer->data[info->len] = '\0';
                client->_tempObject = buffer;
            }

            auto *buffer = static_cast<SetupWsMessageBuffer *>(client->_tempObject);
            if (!buffer) {
                return;
            }

            if (!buffer->data || (info->index + len) > buffer->capacity) {
                delete buffer;
                client->_tempObject = nullptr;
                return;
            }

            memcpy(buffer->data + info->index, data, len);

            if (info->final && (info->index + len == info->len)) {
                buffer->data[info->len] = '\0';
                handleSetupWebSocketMessage(client, reinterpret_cast<const uint8_t *>(buffer->data), info->len);
                delete buffer;
                client->_tempObject = nullptr;
            }
            break;
        }
        case WS_EVT_DISCONNECT:
            delete static_cast<SetupWsMessageBuffer *>(client->_tempObject);
            client->_tempObject = nullptr;
            dispatchSetupJob([this]() {
                releaseSetupWebSocketIfIdle();
            });
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}

void AsyncFsWebServer::handleSetupWebSocketMessage(AsyncWebSocketClient *client, const uint8_t *data, size_t len) {
    if (!client || !data || len == 0) {
        return;
    }

    cJSON *root = cJSON_ParseWithLengthOpts(reinterpret_cast<const char *>(data), len, nullptr, 0);
    if (!root) {
        sendSetupWsResponse(client, String(), false, "invalid", String(), "Invalid JSON");
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *reqId = cJSON_GetObjectItemCaseSensitive(root, "reqId");

    String reqIdStr = cJSON_IsString(reqId) && reqId->valuestring ? String(reqId->valuestring) : String();
    String nameStr = cJSON_IsString(name) && name->valuestring ? String(name->valuestring) : String();

    if (!cJSON_IsString(type) || strcmp(type->valuestring, "cmd") != 0 || nameStr.length() == 0) {
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, false, "invalid", String(), "Invalid command envelope");
        return;
    }

    if (nameStr == "status.get") {
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str(), buildSetupStatusPayload());
        return;
    }

    if (nameStr == "config.get") {
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str(), buildSetupConfigPayload());
        return;
    }

    if (nameStr == "credentials.get") {
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str(), buildSetupCredentialsPayload());
        return;
    }

    if (nameStr == "credentials.delete") {
        bool ok = false;
        if (m_credentialManager && cJSON_IsObject(payload)) {
            cJSON *index = cJSON_GetObjectItemCaseSensitive(payload, "index");
            if (cJSON_IsNumber(index)) {
                ok = m_credentialManager->removeCredential(static_cast<uint8_t>(index->valuedouble));
            }
        }
        if (ok) {
#if defined(ESP32)
            m_credentialManager->saveToNVS();
#elif defined(ESP8266)
            m_credentialManager->saveToFS();
#endif
        }
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, ok, nameStr.c_str(), buildSetupCredentialsPayload(), ok ? String() : String("Invalid credential index"));
        return;
    }

    if (nameStr == "credentials.clear") {
        bool ok = m_credentialManager != nullptr;
        if (m_credentialManager) {
            m_credentialManager->clearAll();
#if defined(ESP32)
            m_credentialManager->saveToNVS();
#elif defined(ESP8266)
            m_credentialManager->saveToFS();
#endif
        }
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, ok, nameStr.c_str(), buildSetupCredentialsPayload(), ok ? String() : String("Credential manager not available"));
        return;
    }

    if (nameStr == "wifi.scan") {
        WiFiScanResult scan = WiFiService::scanNetworks();
        cJSON *scanPayload = cJSON_CreateObject();
        if (scan.reload) {
            cJSON_AddBoolToObject(scanPayload, "reload", true);
        } else {
            cJSON *networks = cJSON_Parse(scan.json.c_str());
            if (!networks) {
                networks = cJSON_CreateArray();
            }
            cJSON_AddItemToObject(scanPayload, "networks", networks);
        }
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str(), serializeJsonDocument(scanPayload));
        return;
    }

    if (nameStr == "config.save") {
        bool ok = false;
        if (cJSON_IsObject(payload)) {
            cJSON *configNode = cJSON_GetObjectItemCaseSensitive(payload, "config");
            if (configNode) {
                char *rawConfig = cJSON_Print(configNode);
                if (rawConfig) {
                    ok = saveSetupConfigJson(String(rawConfig));
                    free(rawConfig);
                }
            }
        }
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, ok, nameStr.c_str(), buildSetupConfigPayload(), ok ? String() : String("Failed to save config"));
        return;
    }

    if (nameStr == "device.restart") {
        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str());
        dispatchSetupJob([]() {
#if defined(ESP8266)
            ESP.reset();
#else
            ESP.restart();
#endif
        });
        return;
    }

    if (nameStr == "wifi.connect") {
        WiFiConnectParams params;
        bool persistent = true;
        bool confirmed = false;

        if (cJSON_IsObject(payload)) {
            cJSON *ssid = cJSON_GetObjectItemCaseSensitive(payload, "ssid");
            cJSON *password = cJSON_GetObjectItemCaseSensitive(payload, "password");
            cJSON *host = cJSON_GetObjectItemCaseSensitive(payload, "hostname");
            cJSON *dhcp = cJSON_GetObjectItemCaseSensitive(payload, "dhcp");
            cJSON *persistentNode = cJSON_GetObjectItemCaseSensitive(payload, "persistent");
            cJSON *confirmedNode = cJSON_GetObjectItemCaseSensitive(payload, "confirmed");
            cJSON *ip = cJSON_GetObjectItemCaseSensitive(payload, "ip_address");
            cJSON *gateway = cJSON_GetObjectItemCaseSensitive(payload, "gateway");
            cJSON *subnet = cJSON_GetObjectItemCaseSensitive(payload, "subnet");
            cJSON *dns1 = cJSON_GetObjectItemCaseSensitive(payload, "dns1");
            cJSON *dns2 = cJSON_GetObjectItemCaseSensitive(payload, "dns2");

            if (cJSON_IsString(ssid) && ssid->valuestring) {
                strlcpy(params.config.ssid, ssid->valuestring, sizeof(params.config.ssid));
            }
            if (cJSON_IsString(password) && password->valuestring) {
                params.password = password->valuestring;
            }
            if (cJSON_IsString(host) && host->valuestring) {
                String requestedHost = String(host->valuestring);
                requestedHost.trim();
                if (requestedHost.length() > 32) {
                    requestedHost.remove(32);
                }
                if (requestedHost.length()) {
                    m_host = requestedHost;
                    if (m_credentialManager) {
                        m_credentialManager->setHostname(m_host.c_str());
                    }
                }
            }
            params.dhcp = !cJSON_IsBool(dhcp) || cJSON_IsTrue(dhcp);
            persistent = !cJSON_IsBool(persistentNode) || cJSON_IsTrue(persistentNode);
            confirmed = cJSON_IsBool(confirmedNode) && cJSON_IsTrue(confirmedNode);

            if (!params.dhcp) {
                if (cJSON_IsString(ip) && ip->valuestring) params.config.local_ip.fromString(ip->valuestring);
                if (cJSON_IsString(gateway) && gateway->valuestring) params.config.gateway.fromString(gateway->valuestring);
                if (cJSON_IsString(subnet) && subnet->valuestring) params.config.subnet.fromString(subnet->valuestring);
                if (cJSON_IsString(dns1) && dns1->valuestring) params.config.dns1.fromString(dns1->valuestring);
                if (cJSON_IsString(dns2) && dns2->valuestring) params.config.dns2.fromString(dns2->valuestring);
            }
        }

        if (params.password.length() == 0 && strlen(params.config.ssid) && m_credentialManager && m_credentialManager->checkSSIDExists(params.config.ssid)) {
            String stored = m_credentialManager->getPassword(params.config.ssid);
            if (stored.length()) {
                params.password = stored;
            }
        }

        bool wasStaConnected = (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP);
        params.fromApClient = m_isApMode;
        params.host = m_host;
        params.timeout = m_timeout;
        params.wdtLongTimeout = AWS_LONG_WDT_TIMEOUT;
        params.wdtTimeout = AWS_WDT_TIMEOUT;

        if (!params.fromApClient && !confirmed && WiFi.status() == WL_CONNECTED) {
            cJSON *confirmPayload = cJSON_CreateObject();
            cJSON_AddBoolToObject(confirmPayload, "confirmRequired", true);
            cJSON_AddStringToObject(confirmPayload, "ssid", params.config.ssid);
            cJSON_Delete(root);
            sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str(), serializeJsonDocument(confirmPayload));
            return;
        }

        cJSON_Delete(root);
        sendSetupWsResponse(client, reqIdStr, true, nameStr.c_str(), String("{\"accepted\":true}"));
        runSetupWifiConnect(client->id(), params, persistent, wasStaConnected && !params.fromApClient, params.fromApClient);
        return;
    }

    cJSON_Delete(root);
    sendSetupWsResponse(client, reqIdStr, false, nameStr.c_str(), String(), "Unknown command");
}

void AsyncFsWebServer::sendSetupWsResponse(AsyncWebSocketClient *client, const String& reqId, bool ok, const char *name, const String& payload, const String& error) {
    if (!client || !client->canSend()) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "res");
    cJSON_AddStringToObject(root, "name", name ? name : "unknown");
    if (reqId.length()) {
        cJSON_AddStringToObject(root, "reqId", reqId.c_str());
    }
    cJSON_AddBoolToObject(root, "ok", ok);
    if (payload.length()) {
        cJSON *payloadNode = cJSON_Parse(payload.c_str());
        if (payloadNode) {
            cJSON_AddItemToObject(root, "payload", payloadNode);
        }
    }
    if (error.length()) {
        cJSON_AddStringToObject(root, "error", error.c_str());
    }
    client->text(serializeJsonDocument(root));
}

void AsyncFsWebServer::sendSetupWsEvent(AsyncWebSocketClient *client, const char *name, const String& payload) {
    if (!client || !client->canSend()) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "evt");
    cJSON_AddStringToObject(root, "name", name ? name : "unknown");
    if (payload.length()) {
        cJSON *payloadNode = cJSON_Parse(payload.c_str());
        if (payloadNode) {
            cJSON_AddItemToObject(root, "payload", payloadNode);
        }
    }
    client->text(serializeJsonDocument(root));
}

void AsyncFsWebServer::sendSetupWsEventById(uint32_t clientId, const char *name, const String& payload) {
    if (!m_setupWs) {
        return;
    }
    m_setupWs->text(clientId, serializeJsonDocument(([&]() {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "evt");
        cJSON_AddStringToObject(root, "name", name ? name : "unknown");
        if (payload.length()) {
            cJSON *payloadNode = cJSON_Parse(payload.c_str());
            if (payloadNode) {
                cJSON_AddItemToObject(root, "payload", payloadNode);
            }
        }
        return root;
    })()));
}

String AsyncFsWebServer::buildSetupStatusPayload() const {
    cJSON *payload = cJSON_CreateObject();
    cJSON *status = cJSON_CreateObject();
    cJSON_AddStringToObject(status, "firmware", m_version.c_str());

    String mode;
    if (WiFi.status() == WL_CONNECTED) {
        mode = "Station (";
        mode += WiFi.SSID();
        mode += ")";
    } else {
        mode = "Access Point";
    }
    cJSON_AddStringToObject(status, "mode", mode.c_str());
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    cJSON_AddStringToObject(status, "ip", ip.c_str());
    cJSON_AddStringToObject(status, "hostname", m_host.c_str());
    cJSON_AddStringToObject(status, "path", String(ESP_FS_WS_CONFIG_FILE).substring(1).c_str());
    cJSON_AddStringToObject(status, "liburl", LIB_URL);

    String logoPath;
    if (const_cast<AsyncFsWebServer *>(this)->getSetupConfigurator()->getOptionValue("img-logo", logoPath) && logoPath.length() > 0) {
        cJSON_AddStringToObject(status, "img-logo", logoPath.c_str());
    }

    String pageTitle;
    if (const_cast<AsyncFsWebServer *>(this)->getSetupConfigurator()->getOptionValue("page-title", pageTitle) && pageTitle.length() > 0) {
        cJSON_AddStringToObject(status, "page-title", pageTitle.c_str());
    }

    cJSON_AddItemToObject(payload, "status", status);
    return serializeJsonDocument(payload);
}

String AsyncFsWebServer::buildSetupConfigPayload() const {
    cJSON *payload = cJSON_CreateObject();
    cJSON *config = nullptr;
    if (m_filesystem && m_filesystem->exists(ESP_FS_WS_CONFIG_FILE)) {
        File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "r");
        if (file) {
            String content = file.readString();
            file.close();
            config = cJSON_Parse(content.c_str());
        }
    }
    if (!config) {
        config = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(payload, "config", config);
    return serializeJsonDocument(payload);
}

String AsyncFsWebServer::buildSetupCredentialsPayload() const {
    cJSON *payload = cJSON_CreateObject();
    cJSON *credentials = cJSON_CreateArray();

    if (m_credentialManager) {
        std::vector<WiFiCredential> *creds = m_credentialManager->getCredentials();
        if (creds) {
            for (size_t i = 0; i < creds->size(); ++i) {
                const WiFiCredential &cred = (*creds)[i];
                cJSON *item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "index", static_cast<double>(i));
                cJSON_AddStringToObject(item, "ssid", cred.ssid);
                if (cred.local_ip != IPAddress(0, 0, 0, 0)) {
                    cJSON_AddStringToObject(item, "ip", cred.local_ip.toString().c_str());
                    cJSON_AddStringToObject(item, "gateway", cred.gateway.toString().c_str());
                    cJSON_AddStringToObject(item, "subnet", cred.subnet.toString().c_str());
                    if (cred.dns1 != IPAddress(0, 0, 0, 0)) {
                        cJSON_AddStringToObject(item, "dns1", cred.dns1.toString().c_str());
                    }
                    if (cred.dns2 != IPAddress(0, 0, 0, 0)) {
                        cJSON_AddStringToObject(item, "dns2", cred.dns2.toString().c_str());
                    }
                }
                cJSON_AddBoolToObject(item, "hasPassword", cred.pwd_len > 0);
                cJSON_AddItemToArray(credentials, item);
            }
        }
    }

    cJSON_AddItemToObject(payload, "credentials", credentials);
    return serializeJsonDocument(payload);
}

bool AsyncFsWebServer::saveSetupConfigJson(const String& jsonText) {
    if (!m_filesystem) {
        return false;
    }

    File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "w");
    if (!file) {
        return false;
    }

    size_t written = file.print(jsonText);
    file.close();

    if (written == 0) {
        return false;
    }

    if (m_configSavedCallback) {
        m_configSavedCallback(ESP_FS_WS_CONFIG_FILE);
    }
    return true;
}

void AsyncFsWebServer::runSetupWifiConnect(uint32_t clientId, WiFiConnectParams params, bool persistent, bool allowApFallback, bool fromApClient) {
    sendSetupWsEventById(clientId, "wifi.connect.started", String("{\"ssid\":\"") + params.config.ssid + "\"}");

    auto connectJob = [this, clientId, params, persistent, allowApFallback, fromApClient]() mutable {
        WiFiConnectResult result = WiFiService::connectWithParams(params);

        if (result.connected && persistent && strlen(params.config.ssid) && params.password.length() && m_credentialManager) {
            if (params.dhcp) {
                params.config.local_ip = IPAddress(0, 0, 0, 0);
                params.config.gateway = IPAddress(0, 0, 0, 0);
                params.config.subnet = IPAddress(0, 0, 0, 0);
                params.config.dns1 = IPAddress(0, 0, 0, 0);
                params.config.dns2 = IPAddress(0, 0, 0, 0);
            }
            if (!m_credentialManager->updateCredential(params.config, params.password.c_str())) {
                m_credentialManager->addCredential(params.config, params.password.c_str());
            }
#if defined(ESP32)
            m_credentialManager->saveToNVS();
#elif defined(ESP8266)
            m_credentialManager->saveToFS();
#endif
        }

        if (result.connected) {
            m_serverIp = result.ip;
        } else if (allowApFallback && strlen(m_apSSID) > 0) {
            startCaptivePortal(m_apSSID, m_apPassword, "/setup");
        }

        cJSON *payload = cJSON_CreateObject();
        cJSON_AddBoolToObject(payload, "connected", result.connected);
        cJSON_AddNumberToObject(payload, "status", result.status);
        cJSON_AddStringToObject(payload, "body", result.body.c_str());
        cJSON_AddStringToObject(payload, "ip", result.ip.toString().c_str());
        cJSON_AddStringToObject(payload, "hostname", m_host.c_str());
        cJSON_AddBoolToObject(payload, "fromApClient", fromApClient);

        sendSetupWsEventById(clientId, result.connected ? "wifi.connect.success" : "wifi.connect.error", serializeJsonDocument(payload));
    };

    dispatchSetupJob(std::move(connectJob));
}
#endif

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

void AsyncFsWebServer::handleIndex(AsyncWebServerRequest *request) {
    if (m_authAll && m_pageUser != nullptr) {
        if(!request->authenticate(m_pageUser, m_pagePswd))
            return request->requestAuthentication();
    }

    const char *indexCandidates[] = {
        "/index.htm",
        "/index.html",
        "/index.htm.gz",
        "/index.html.gz"
    };

    for (const char *candidate : indexCandidates) {
        if (!m_filesystem->exists(candidate)) {
            continue;
        }
        log_debug("Serving %s for /", candidate);
        String path = candidate;
        request->redirect(path);
        return;
    }

#if ESP_FS_WS_SETUP
    log_debug("No index file found, redirecting / to /setup");
    request->redirect("/setup");
#else
    request->send(404, "text/plain", "AsyncFsWebServer: resource not found");
#endif
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
    if (request->url() != "/setup") {
        const String path = request->url();
        const String logoPath = String(ESP_FS_WS_CONFIG_FOLDER) + "/logo.svg";
        const String logoBase = String(ESP_FS_WS_CONFIG_FOLDER) + "/logo";

        if (path == logoPath &&
            !m_filesystem->exists(logoBase + ".svg") &&
            !m_filesystem->exists(logoBase + ".svg.gz") &&
            !m_filesystem->exists(logoBase + ".png") &&
            !m_filesystem->exists(logoBase + ".jpg") &&
            !m_filesystem->exists(logoBase + ".gif")) {
            AsyncWebServerResponse *response = request->beginResponse(200, "image/svg+xml", (uint8_t*)_aclogo_svg, sizeof(_aclogo_svg));
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "public, max-age=86400");
            request->send(response);
            return;
        }

        if (m_filesystem->exists(path)) {
            request->send(*m_filesystem, path);
            return;
        }

        const String gzPath = path + ".gz";
        if (m_filesystem->exists(gzPath)) {
            request->redirect(gzPath);
            return;
        }

        notFound(request);
        return;
    }

    if (m_pageUser != nullptr) {
        if(!request->authenticate(m_pageUser, m_pagePswd))
            return request->requestAuthentication();
    }

    initSetupWebSocket();

    // Changed array name to match SEGGER Bin2C output
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)_acsetup_min_htm, sizeof(_acsetup_min_htm));
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("X-Config-File", ESP_FS_WS_CONFIG_FILE);
    response->addHeader("Set-Cookie", "esp_fs_ws_mode=same-origin; Path=/; SameSite=Lax");
    request->send(response);
}
#endif

#if ESP_FS_WS_SETUP
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
    strncpy(params.config.ssid, ssid, sizeof(params.config.ssid) - 1);
    params.config.ssid[sizeof(params.config.ssid) - 1] = '\0';
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

