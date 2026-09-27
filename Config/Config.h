#pragma once
#include <string>

// All the settings that used to be hardcoded across Server.cpp and
// ServerManager.cpp, now in one place so a new hosting target only
// means editing server.properties, never recompiling.
struct ServerConfig {
    unsigned short port = 19132;

    // "0.0.0.0" (the default) means "listen on every network interface" -
    // LAN, localhost, AND the public internet, all at the same time. This
    // is already correct for every hosting target (home PC, VPS, tunnel)
    // and does NOT need to change between "local" and "public" - that's
    // handled by the router/firewall/tunnel in front of the socket, not
    // by this address. Only override it (e.g. to "127.0.0.1") if you
    // deliberately want to block everything except local connections.
    std::string bindAddress = "0.0.0.0";

    std::string motd = "LBCore Bedrock Server";

    // This is just the world's NAME, not a path. The actual folder that
    // gets created/opened on disk is always "worlds/<levelName>" (see
    // executer.cpp) — matching the PocketMine/most-server-software
    // convention of one top-level "worlds" directory holding every
    // world, rather than dumping world folders next to the executable.
    std::string levelName = "World";
    std::string gamemode = "Survival";
    int maxPlayers = 10;
    int protocolVersion = 2193;
    std::string versionName = "26.51";

    // Registers the built-in commands (/help /list /pos /version) in
    // AvailableCommands. Set to false to send the original empty command list.
    bool enableCommands = true;
};

// Reads key=value pairs from a properties file (default:
// "server.properties", looked up relative to the working directory the
// server is launched from). Missing file or missing keys silently fall
// back to the defaults above, so the server always starts. Call this
// once, early in main(), before start()/LBStart()/LBBStart().
void loadConfig(const std::string& path = "server.properties");

// Read-only access to whatever was loaded (or the defaults, if
// loadConfig() was never called or found nothing).
const ServerConfig& getConfig();
