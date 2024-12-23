#include "common.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <netdb.h>

using json = nlohmann::json;

bool sendResultToMaster(int workerId, const std::vector<double>& distances) {
    const char* masterHostname = "master";

    struct hostent* he = gethostbyname(masterHostname);
    if (!he) {
        std::cerr << "[Worker] Failed to resolve master hostname: " << hstrerror(h_errno) << "\n";
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "[Worker] Failed to create socket: " << strerror(errno) << "\n";
        return false;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[Worker] Failed to connect to master: " << strerror(errno) << "\n";
        close(sock);
        return false;
    }

    try {
        Message msg;
        msg.magic = htonl(MAGIC_NUMBER);

        json j;
        j["worker_id"] = workerId;
        j["distances"] = distances;
        std::string data = j.dump();

        msg.size = htonl(static_cast<uint32_t>(data.size()));

        // Send the header
        if (send(sock, &msg, sizeof(msg), 0) != sizeof(msg)) {
            std::cerr << "[Worker] Failed to send header\n";
            close(sock);
            return false;
        }

        // Send the data
        if (send(sock, data.c_str(), data.size(), 0) != static_cast<ssize_t>(data.size())) {
            std::cerr << "[Worker] Failed to send data\n";
            close(sock);
            return false;
        }

        char response[16];
        ssize_t received = recv(sock, response, sizeof(response), 0);
        if (received > 0) {
            response[received] = '\0';
            std::cout << "[Worker] Master response: " << response << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[Worker] Error: " << e.what() << "\n";
    }

    close(sock);
    return true;
}
