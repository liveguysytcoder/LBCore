#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

using namespace std;

static ServerConfig g_config;

static string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void loadConfig(const string& path) {
    ifstream file(path);

    if (!file.is_open()) {
        cout << "[Config] No " << path
             << " found - using built-in defaults (port "
             << g_config.port << ", bind " << g_config.bindAddress << ")\n";
        return;
    }

    string line;
    int lineNumber = 0;

    while (getline(file, line)) {
        lineNumber++;
        string trimmed = trim(line);

        // Skip blank lines and comments
        if (trimmed.empty() || trimmed[0] == '#') continue;

        size_t eq = trimmed.find('=');
        if (eq == string::npos) {
            cout << "[Config] Ignoring malformed line " << lineNumber
                 << " in " << path << " (no '=')\n";
            continue;
        }

        string key = trim(trimmed.substr(0, eq));
        string value = trim(trimmed.substr(eq + 1));

        try {
            if (key == "port") {
                g_config.port = static_cast<unsigned short>(stoi(value));
            } else if (key == "bind-address") {
                g_config.bindAddress = value;
            } else if (key == "motd") {
                g_config.motd = value;
            } else if (key == "level-name") {
                g_config.levelName = value;
            } else if (key == "gamemode") {
                g_config.gamemode = value;
            } else if (key == "max-players") {
                g_config.maxPlayers = stoi(value);
            } else if (key == "protocol-version") {
                g_config.protocolVersion = stoi(value);
            } else if (key == "version-name") {
                g_config.versionName = value;
            } else if (key == "enable-commands") {
                g_config.enableCommands = (value == "true" || value == "1" || value == "yes");
            } else {
                cout << "[Config] Unknown key '" << key << "' on line "
                     << lineNumber << " - ignored\n";
            }
        } catch (const exception& e) {
            cout << "[Config] Bad value for '" << key << "' on line "
                 << lineNumber << " - keeping default\n";
        }
    }

    cout << "[Config] Loaded " << path << " -> port " << g_config.port
         << ", bind " << g_config.bindAddress << ", motd \""
         << g_config.motd << "\"\n";
}

const ServerConfig& getConfig() {
    return g_config;
}
