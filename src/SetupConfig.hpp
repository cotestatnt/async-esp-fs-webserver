#ifndef CONFIGURATOR_HPP
#define CONFIGURATOR_HPP
#include <type_traits>
#include <FS.h>

#include "Json.h"
#include "SerialLog.h"

#define MIN_F -3.4028235E+38
#define MAX_F 3.4028235E+38

class SetupConfigurator
{
    protected:
        uint8_t numOptions = 0;
        fs::FS* m_filesystem = nullptr;
        AsyncFSWebServer::Json* m_doc = nullptr;
        AsyncFSWebServer::Json* m_savedDoc = nullptr;  // Temporary storage for saved file values

        bool m_opened = false;

        bool isOpened() {
            return m_opened;
        }

        bool openConfiguration() {
            if (checkConfigFile()) {
                // Read existing file into m_savedDoc (background copy for value lookup)
                if (m_filesystem->exists(ESP_FS_WS_CONFIG_FILE)) {
                    File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "r");
                    if (file) {
                        String content = file.readString();
                        file.close();
                        
                        m_savedDoc = new AsyncFSWebServer::Json();
                        if (!m_savedDoc->parse(content)) {
                            log_error("Failed to parse existing configuration");
                            delete m_savedDoc;
                            m_savedDoc = nullptr;
                            // Don't continue if parsing fails
                            return false;
                        }
                    }
                }
                
                // Create empty m_doc - will be populated in addOption() in the setup order
                m_doc = new AsyncFSWebServer::Json();
                m_doc->setString("wifi-box", "");
                m_doc->setBool("dhcp", false);
                
                m_opened = true;
                return true;
            }
            return false;
        }

        bool checkConfigFile() {
            File file = m_filesystem->open(ESP_FS_WS_CONFIG_FOLDER, "r");
            if (!file) {
                log_error("Failed to open /setup directory. Create new folder\n");
                if (!m_filesystem->mkdir(ESP_FS_WS_CONFIG_FOLDER)) {
                    log_error("Error. Folder %s not created", ESP_FS_WS_CONFIG_FOLDER);
                    return false;
                }
            }

            // Check if config file exist, and create if necessary
            if (!m_filesystem->exists(ESP_FS_WS_CONFIG_FILE)) {
                file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "w");
                if (!file) {
                    log_error("Error. File %s not created", ESP_FS_WS_CONFIG_FILE);
                    return false;
                }
                file.println("{\"wifi-box\": \"\", \"dhcp\": false}");
                file.close();
            }
            log_debug("Config file %s OK", ESP_FS_WS_CONFIG_FILE);
            return true;
        }

    public:
        friend class AsyncFsWebServer;
        SetupConfigurator(fs::FS *fs) : m_filesystem(fs) { ; }

        bool closeConfiguration() {
            m_opened = false;

            // Serialize the new content
            String newContent = m_doc->serialize(true);
            
            // Read existing file content
            String oldContent;
            if (m_filesystem->exists(ESP_FS_WS_CONFIG_FILE)) {
                File readFile = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "r");
                if (readFile) {
                    oldContent = readFile.readString();
                    readFile.close();
                }
            }
            
            // Write only if content is different
            if (oldContent != newContent) {
                File file = m_filesystem->open(ESP_FS_WS_CONFIG_FILE, "w");
                if (file) {
                    file.print(newContent);
                    file.close();
                    log_debug("Config file written (content changed)");
                } 
                else {
                    log_error("Error opening config file for write");
                    delete (m_doc);
                    m_doc = nullptr;
                    if (m_savedDoc) { 
                        delete (m_savedDoc); 
                        m_savedDoc = nullptr; 
                    }
                    return false;
                }
            } 
            else {
                log_debug("Config file unchanged, skipping write");
            }
            
            delete (m_doc);
            m_doc = nullptr;
            if (m_savedDoc) { 
                delete (m_savedDoc); 
                m_savedDoc = nullptr; 
            }
            return true;
        }

        void setLogoBase64(const char* logo, const char* width, const char* height, bool overwrite) {
            // Use snprintf to safely create the filename with proper bounds checking
            char filename[128];
            int written = snprintf(filename, sizeof(filename), "%s/img-logo-%s_%s.txt", 
                                   ESP_FS_WS_CONFIG_FOLDER, width, height);
            
            if (written < 0 || written >= (int)sizeof(filename)) {
                log_error("Logo filename too long");
                return;
            }

            optionToFile(filename, logo, overwrite);
            addOption("img-logo", filename);
        }

        bool optionToFile(const char* filename, const char* str, bool overWrite) {
            // Check if file is already saved
            if (m_filesystem->exists(filename) && !overWrite) {
                return true;
            }
            // Create or overwrite option file
            else {
                File file = m_filesystem->open(filename, "w");
                if (file) {
                    #if defined(ESP8266)
                    String _str = str;
                    file.print(_str);
                    #else
                    file.print(str);
                    #endif
                    file.close();
                    log_debug("File %s saved", filename);
                    return true;
                }
                else {
                    log_debug("Error writing file %s", filename);
                }
            }
            return false;
        }

        void addSource(const String& source, const String& tag, bool overWrite) {
            if (m_doc == nullptr) {
                if (!openConfiguration()) {
                    log_error("Error! /setup configuration not possible");
                }
            }

            String path = ESP_FS_WS_CONFIG_FOLDER;
            path += "/";
            path += tag;

            if (tag.indexOf("html") > -1)
                path += ".htm";
            else if (tag.indexOf("css") > -1)
                path += ".css";
            else if (tag.indexOf("javascript") > -1)
                path += ".js";

            if (optionToFile(path.c_str(), source.c_str(), overWrite)){
                m_doc->setString(tag.c_str(), path.c_str());
            }
            else {
                log_error("Source option not saved");
            }

        }

        void addHTML(const char* html, const char* id, bool overWrite) {
            String _id = "raw-html-";
            _id  += id;
            String source = html;
            addSource(source, _id, overWrite);
        }

        void addCSS(const char* css,  const char* id, bool overWrite) {
            String _id = "raw-css-" ;
            _id  += id;
            String source = css;
            addSource(source, _id, overWrite);
        }

        void addJavascript(const char* script,  const char* id, bool overWrite) {
            String _id = "raw-javascript-" ;
            _id  += id;
            String source = script;
            addSource(source, _id, overWrite);
        }


        /*
            Add a new dropdown input element
        */
        void addDropdownList(const char *label, const char** array, size_t size) {

            // If key is present we don't need to create it.
            if (m_doc->hasObject(label)) {
                log_debug("Key \"%s\" value present", label);
                return;
            }
            m_doc->ensureObject(label);
            
            // Try to get saved "selected" value from m_savedDoc, otherwise use first item
            String selectedValue = String(array[0]);
            if (m_savedDoc) {
                String savedSelected;
                if (m_savedDoc->getString(label, "selected", savedSelected)) {
                    selectedValue = savedSelected;
                    log_debug("Dropdown \"%s\" using saved value: %s", label, selectedValue.c_str());
                }
            }
            
            m_doc->setString(label, "selected", selectedValue);
            std::vector<String> vals; vals.reserve(size);
            for (unsigned int i=0; i<size; i++) { vals.emplace_back(String(array[i])); }
            m_doc->setArray(label, "values", vals);

            numOptions++ ;
        }

        /*
            Add a new option box with custom label
        */
        void addOptionBox(const char* boxTitle) {
            addOption("param-box", boxTitle, false);
        }

        /*
            Add custom option to config webpage (float values)
        */
        template <typename T>
        void addOption(const char *label, T val, double d_min, double d_max, double step) {
            addOption(label, val, false, d_min, d_max, step);
        }

        /*
        Add custom option to config webpage (type of parameter will be deduced from variable itself)
        */
        template <typename T>
        void addOption(const char *label, T val, bool hidden = false,
                            double d_min = MIN_F, double d_max = MAX_F, double step = 1.0)
        {

            if (m_doc == nullptr) {
                if (!openConfiguration()) {
                    log_error("Error! /setup configuration not possible");
                }
            }
            log_debug("Adding option \"%s\"", label);

            String key = label;
            String savedKey = key; // original label for lookup in saved file
            if (hidden)
                key += "-hidden";
            // Univoque key name
            if (key.equals("param-box"))
                key += numOptions ;
            if (key.equals("raw-javascript"))
                key += numOptions ;

            // If key is present we don't need to create it
            if (m_doc->hasObject(key.c_str())) {
                log_debug("Key \"%s\" already exists, skipping", key.c_str());
                numOptions++;
                return;
            }
            
            // Try to get saved value from m_savedDoc, otherwise use provided default
            bool valueFromSaved = false;
            
            // if min, max, step != from default, treat this as object in order to set other properties
            if (d_min != MIN_F || d_max != MAX_F || step != 1.0) {
                m_doc->ensureObject(key.c_str());
                
                if constexpr (std::is_same<T, String>::value) {
                    String savedVal;
                    if (m_savedDoc && (m_savedDoc->getString(key.c_str(), "value", savedVal) || m_savedDoc->getString(savedKey.c_str(), "value", savedVal))) {
                        m_doc->setString(key.c_str(), "value", savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setString(key.c_str(), "value", val);
                    }
                } else if constexpr (std::is_same<T, const char*>::value || std::is_same<T, char*>::value) {
                    String savedVal;
                    if (m_savedDoc && (m_savedDoc->getString(key.c_str(), "value", savedVal) || m_savedDoc->getString(savedKey.c_str(), "value", savedVal))) {
                        m_doc->setString(key.c_str(), "value", savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setString(key.c_str(), "value", String(val));
                    }
                } else {
                    double savedVal;
                    if (m_savedDoc && (m_savedDoc->getNumber(key.c_str(), "value", savedVal) || m_savedDoc->getNumber(savedKey.c_str(), "value", savedVal))) {
                        m_doc->setNumber(key.c_str(), "value", savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setNumber(key.c_str(), "value", static_cast<double>(val));
                    }
                }
                
                // min, max, step always use defaults (not persisted per se)
                m_doc->setNumber(key.c_str(), "min", d_min);
                m_doc->setNumber(key.c_str(), "max", d_max);
                m_doc->setNumber(key.c_str(), "step", step);
            }
            else {
                if constexpr (std::is_same<T, String>::value) {
                    String savedVal;
                    if (m_savedDoc && (m_savedDoc->getString(key.c_str(), savedVal) || m_savedDoc->getString(savedKey.c_str(), savedVal))) {
                        m_doc->setString(key.c_str(), savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setString(key.c_str(), val);
                    }
                } else if constexpr (std::is_same<T, const char*>::value || std::is_same<T, char*>::value) {
                    String savedVal;
                    if (m_savedDoc && (m_savedDoc->getString(key.c_str(), savedVal) || m_savedDoc->getString(savedKey.c_str(), savedVal))) {
                        m_doc->setString(key.c_str(), savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setString(key.c_str(), String(val));
                    }
                } else if constexpr (std::is_same<T, bool>::value) {
                    // Handle bool as boolean JSON type, not number
                    bool savedVal;
                    if (m_savedDoc && (m_savedDoc->getBool(key.c_str(), savedVal) || m_savedDoc->getBool(savedKey.c_str(), savedVal))) {
                        m_doc->setBool(key.c_str(), savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setBool(key.c_str(), val);
                    }
                } else {
                    double savedVal;
                    if (m_savedDoc && (m_savedDoc->getNumber(key.c_str(), savedVal) || m_savedDoc->getNumber(savedKey.c_str(), savedVal))) {
                        m_doc->setNumber(key.c_str(), savedVal);
                        valueFromSaved = true;
                    } else {
                        m_doc->setNumber(key.c_str(), static_cast<double>(val));
                    }
                }
            }
            
            log_debug("Value added (saved=%d)", valueFromSaved);
            numOptions++;
        }

        /*
            Get current value for a specific custom option (true on success)
            Reads from m_doc if open, or reloads from file if closed
        */
        template <typename T>
        bool getOptionValue(const char *label, T &var) {
            // If m_doc is nullptr, reload configuration from file
            if (m_doc == nullptr) {
                if (!openConfiguration()) {
                    log_error("Error! /setup configuration not possible");
                    return false;
                }
            }
            
            // Prefer persisted values when available; fall back to current session doc
            AsyncFSWebServer::Json* sourceDoc = (m_savedDoc != nullptr) ? m_savedDoc : m_doc;
            
            if (sourceDoc == nullptr) {
                log_error("No configuration document available for reading");
                return false;
            }

            if constexpr (std::is_same<T, String>::value) {
                String out;
                if (sourceDoc->getString(label, "value", out)) var = out;
                else if (sourceDoc->getString(label, "selected", out)) var = out;
                else if (sourceDoc->getString(label, out)) var = out;
            } else if constexpr (std::is_same<T, const char*>::value || std::is_same<T, char*>::value) {
                // For C-strings, read as string and assign to var via const char*
                String out;
                if (sourceDoc->getString(label, "value", out)) var = out.c_str();
                else if (sourceDoc->getString(label, "selected", out)) var = out.c_str();
                else if (sourceDoc->getString(label, out)) var = out.c_str();
            } else {
                double out;
                if (sourceDoc->getNumber(label, "value", out)) var = static_cast<T>(out);
                else if (sourceDoc->getNumber(label, "selected", out)) var = static_cast<T>(out);
                else if (sourceDoc->getNumber(label, out)) var = static_cast<T>(out);
            }
            return true;
        }

        template <typename T>
        bool saveOptionValue(const char *label, T val) {
            if (m_doc == nullptr) {
                if (!openConfiguration()) {
                    log_error("Error! /setup configuration not possible");
                    return false;
                }
            }

            if constexpr (std::is_same<T, String>::value) {
                String v = val;
                if (m_doc->hasKey(label, "value")) m_doc->setString(label, "value", v);
                else if (m_doc->hasKey(label, "selected")) m_doc->setString(label, "selected", v);
                else m_doc->setString(label, v);
            } else if constexpr (std::is_same<T, const char*>::value || std::is_same<T, char*>::value) {
                String v = String(val);
                if (m_doc->hasKey(label, "value")) m_doc->setString(label, "value", v);
                else if (m_doc->hasKey(label, "selected")) m_doc->setString(label, "selected", v);
                else m_doc->setString(label, v);
            } else if constexpr (std::is_same<T, bool>::value) {
                // Persist booleans as JSON boolean type
                bool v = val;
                if (m_doc->hasKey(label, "value")) m_doc->setBool(label, "value", v);
                else if (m_doc->hasKey(label, "selected")) m_doc->setBool(label, "selected", v);
                else m_doc->setBool(label, v);
            } else {
                double v = static_cast<double>(val);
                if (m_doc->hasKey(label, "value")) m_doc->setNumber(label, "value", v);
                else if (m_doc->hasKey(label, "selected")) m_doc->setNumber(label, "selected", v);
                else m_doc->setNumber(label, v);
            }
            return true;
        }

};

#endif