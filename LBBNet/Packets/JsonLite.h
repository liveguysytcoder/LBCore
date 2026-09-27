#pragma once
#include <string>

using namespace std;

// Best-effort extraction of a `"key":"value"` string field from JSON text.
// This project doesn't pull in a full JSON library — the JWT payloads in
// the Login packet only need a handful of known string fields out of them
// (XUID, displayName, identity), so this scans for the key and reads the
// quoted value that follows it directly, instead of parsing the whole
// document into a tree. Pass a substring (e.g. just the "extraData" object)
// to scope the search if the same key could appear elsewhere in the JSON.
string jsonExtractString(const string& json, const string& key);

// Best-effort extraction of a `"key":123` or `"key":-5` integer field
// (unquoted numeric literal). Returns 0 if the key is missing or the value
// isn't a plain integer -- good enough for the width/height fields in the
// Login packet's client-data JSON, which are always plain integers.
long long jsonExtractInt(const string& json, const string& key);

// Best-effort extraction of a `"key":true`/`"key":false` boolean field
// (unquoted true/false literal). Returns defaultValue if the key is
// missing or the value isn't literally true/false.
bool jsonExtractBool(const string& json, const string& key, bool defaultValue);
