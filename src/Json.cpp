#include "Json.h"
#include <cstring>
#include <stdlib.h>

using namespace AsyncFSWebServer;

Json::Json() : root(nullptr) {}
Json::~Json()
{
    if (root)
        cJSON_Delete(root);
}

bool Json::parse(const String &text)
{
    if (root)
        cJSON_Delete(root);
    root = cJSON_Parse(text.c_str());
    return root != nullptr;
}

String Json::serialize(bool /*pretty*/) const
{
    if (!root)
        return String();
    char *out = cJSON_Print(root);
    String s = out ? String(out) : String();
    if (out)
        free(out);
    return s;
}

bool Json::hasObject(const String &key) const
{
    if (!root)
        return false;
    cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    return obj && cJSON_IsObject(obj);
}

void Json::ensureObject(const String &key)
{
    if (!root)
        root = cJSON_CreateObject();
    cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    if (!(obj && cJSON_IsObject(obj)))
    {
        cJSON *n = cJSON_CreateObject();
        cJSON_AddItemToObject(root, key.c_str(), n);
    }
}

// --------- Top-level helpers ---------

bool Json::hasKey(const String &key) const
{
    if (!root) return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    return item != nullptr;
}


bool Json::hasKey(const String &objName, const String &key) const
{
    if (!root)
        return false;
    cJSON *scope = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!scope)
        return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(scope, key.c_str());
    return item != nullptr;
}

bool Json::setString(const String &key, const String &value)
{
    if (!root) root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(root, key.c_str());
    cJSON_AddItemToObject(root, key.c_str(), cJSON_CreateString(value.c_str()));
    return true;
}

bool Json::setNumber(const String &key, double value)
{
    if (!root) root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(root, key.c_str());
    cJSON_AddItemToObject(root, key.c_str(), cJSON_CreateNumber(value));
    return true;
}

bool Json::setBool(const String &key, bool value)
{
    if (!root) root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(root, key.c_str());
    cJSON_AddItemToObject(root, key.c_str(), cJSON_CreateBool(value));
    return true;
}

bool Json::setArray(const String &key, const std::vector<String> &values)
{
    if (!root) root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (auto &v : values) cJSON_AddItemToArray(arr, cJSON_CreateString(v.c_str()));
    cJSON_DeleteItemFromObjectCaseSensitive(root, key.c_str());
    cJSON_AddItemToObject(root, key.c_str(), arr);
    return true;
}


bool Json::setString(const String &objName, const String &key, const String &value)
{
    ensureObject(objName);
    cJSON *target = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!target)
        return false;
    cJSON_DeleteItemFromObjectCaseSensitive(target, key.c_str());
    cJSON_AddItemToObject(target, key.c_str(), cJSON_CreateString(value.c_str()));
    return true;
}

bool Json::setNumber(const String &objName, const String &key, double value)
{
    ensureObject(objName);
    cJSON *target = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!target)
        return false;
    cJSON_DeleteItemFromObjectCaseSensitive(target, key.c_str());
    cJSON_AddItemToObject(target, key.c_str(), cJSON_CreateNumber(value));
    return true;
}

bool Json::setBool(const String &objName, const String &key, bool value)
{
    ensureObject(objName);
    cJSON *target = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!target)
        return false;
    cJSON_DeleteItemFromObjectCaseSensitive(target, key.c_str());
    cJSON_AddItemToObject(target, key.c_str(), cJSON_CreateBool(value));
    return true;
}

bool Json::setArray(const String &objName, const String &key, const std::vector<String> &values)
{
    ensureObject(objName);
    cJSON *target = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!target)
        return false;
    cJSON *arr = cJSON_CreateArray();
    for (auto &v : values)
        cJSON_AddItemToArray(arr, cJSON_CreateString(v.c_str()));
    cJSON_DeleteItemFromObjectCaseSensitive(target, key.c_str());
    cJSON_AddItemToObject(target, key.c_str(), arr);
    return true;
}

bool Json::getString(const String &key, String &out) const
{
    if (!root) return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    if (!item || !cJSON_IsString(item)) return false;
    out = item->valuestring ? String(item->valuestring) : String();
    return true;
}

bool Json::getBool(const String& key, bool& out) const {
    if (!root) return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    if (!item) return false;
    if (cJSON_IsBool(item)) {
        out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        out = (item->valuedouble != 0.0);
        return true;
    }
    return false;
}

bool Json::getNumber(const String &key, double &out) const
{
    if (!root) return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    if (!item) return false;
    if (cJSON_IsNumber(item)) {
        out = item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        out = atof(item->valuestring);
        return true;
    }
    return false;
}


bool Json::getNumber(const String &key, float &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<float>(tmp);
    return true;
}

bool Json::getNumber(const String &key, int8_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<int8_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, uint8_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<uint8_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, int16_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<int16_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, uint16_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<uint16_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, int32_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<int32_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, uint32_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<uint32_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, int64_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<int64_t>(tmp);
    return true;
}

bool Json::getNumber(const String &key, uint64_t &out) const
{
    double tmp;
    if (!getNumber(key, tmp)) return false;
    out = static_cast<uint64_t>(tmp);
    return true;
}

// Object-scoped key helpers
bool Json::getString(const String &objName, const String &key, String &out) const
{
    if (!root)
        return false;
    cJSON *scope = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!scope)
        return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(scope, key.c_str());
    if (!item || !cJSON_IsString(item))
        return false;
    out = item->valuestring ? String(item->valuestring) : String();
    return true;
}

bool Json::getNumber(const String &objName, const String &key, double &out) const
{
    if (!root)
        return false;
    cJSON *scope = cJSON_GetObjectItemCaseSensitive(root, objName.c_str());
    if (!scope)
        return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(scope, key.c_str());
    if (!item)
        return false;
    out = item->valuedouble;
    return true;
}


