#include "JsonLite.h"

string jsonExtractString(const string& json, const string& key) {
    string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == string::npos) return "";

    size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == string::npos) return "";

    size_t quoteStart = json.find('"', colonPos + 1);
    if (quoteStart == string::npos) return "";

    string value;
    size_t i = quoteStart + 1;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            // Skip the escape and keep the escaped character literally;
            // good enough for the plain strings (names, UUIDs, numeric
            // XUIDs) these payloads actually carry.
            value += json[i + 1];
            i += 2;
        } else {
            value += json[i];
            i++;
        }
    }
    return value;
}

long long jsonExtractInt(const string& json, const string& key) {
    string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == string::npos) return 0;

    size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == string::npos) return 0;

    size_t i = colonPos + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) i++;

    bool negative = false;
    if (i < json.size() && json[i] == '-') { negative = true; i++; }

    long long value = 0;
    bool sawDigit = false;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10 + (json[i] - '0');
        sawDigit = true;
        i++;
    }
    if (!sawDigit) return 0;
    return negative ? -value : value;
}

bool jsonExtractBool(const string& json, const string& key, bool defaultValue) {
    string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == string::npos) return defaultValue;

    size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == string::npos) return defaultValue;

    size_t i = colonPos + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) i++;

    if (json.compare(i, 4, "true") == 0) return true;
    if (json.compare(i, 5, "false") == 0) return false;
    return defaultValue;
}
