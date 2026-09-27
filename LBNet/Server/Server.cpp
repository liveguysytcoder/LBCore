#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include "Server.h"
#include "../../LBBNet/Packets/DisconnectPacket.h"
#include "../../Config/Config.h"

using namespace std;

bool running = true;
int serverSocket = -1;
sockaddr_in serverAddress{};

sockaddr_in getServerAddress() {
    return serverAddress;
}

void start() {
    const ServerConfig& config = getConfig();

    cout << "[INFO] Starting UDP listener on " << config.bindAddress
         << ":" << config.port << "...\n";

    struct sockaddr_in serverAddr{}, clientAddr{};
    char buffer[2048];
    socklen_t addrLen = sizeof(clientAddr);

    // Create UDP socket
    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) {
        perror("[ERROR] Socket creation failed");
        return;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(config.port);

    // "0.0.0.0" -> INADDR_ANY, which listens on every interface at once
    // (LAN + localhost + public internet). This is correct for every
    // hosting target and is why there's no separate "local" vs "public"
    // mode - only override bind-address in server.properties if you
    // deliberately want to restrict which interfaces can connect.
    if (config.bindAddress == "0.0.0.0" || config.bindAddress.empty()) {
        serverAddr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config.bindAddress.c_str(), &serverAddr.sin_addr) != 1) {
            cout << "[WARN] Invalid bind-address '" << config.bindAddress
                 << "' - falling back to 0.0.0.0 (all interfaces)\n";
            serverAddr.sin_addr.s_addr = INADDR_ANY;
        }
    }

    if (::bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("[ERROR] Bind failed");
        close(serverSocket);
        return;
    }

    cout << "[READY] Listening for packets on " << config.bindAddress
         << ":" << config.port << "...\n";

    while (running) {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytesReceived = recvfrom(
            serverSocket, buffer, sizeof(buffer), 0,
            (struct sockaddr*)&clientAddr, &addrLen
        );

        if (bytesReceived > 0) {
            cout << "\nReceived " << bytesReceived << " bytes from "
                 << inet_ntoa(clientAddr.sin_addr)
                 << ":" << ntohs(clientAddr.sin_port) << endl;
            
            cout << "RAW(" << bytesReceived << "): ";
            for (ssize_t i = 0; i < bytesReceived; i++)
                printf("%02X ", (unsigned char)buffer[i]);
            cout << "\n";

            vector<unsigned char> data(buffer, buffer + bytesReceived);
            try {
                decodePacket(serverSocket, clientAddr, data);
            } catch (const std::exception& e) {
                // A malformed packet must never take the whole server down.
                cout << "[ERROR] Dropped malformed packet from "
                     << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port)
                     << " (" << e.what() << ")\n";
            }
        }
    }

    disconnectAllClients(68 /* Shutdown */, "Server closed");
    cout << "[INFO] Server stopped.\n";

    if (serverSocket != -1) {
        close(serverSocket);
        serverSocket = -1;
    }
}

void stopServer() {
    cout << "[INFO] Stopping server...\n";
    running = false;
}