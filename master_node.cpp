#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <mutex>
#include <map>
#include <nlohmann/json.hpp>
#include <iomanip>
#include "common.h"

using json = nlohmann::json;

void debug_print(const std::string& msg) {
    std::cout << "[Master] " << msg << std::endl;
    std::cout.flush();
}

void hexDump(const void* data, size_t size) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::cout << "[Master] HEX DUMP [" << size << " bytes]: ";
    for (size_t i = 0; i < size; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(p[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}


class ResultCollector {
private:
    std::map<int, std::vector<double>> results;
    std::mutex mtx;

public:
    void addResult(int workerId, const std::vector<double>& distances) {
        std::lock_guard<std::mutex> lock(mtx);
        results[workerId] = distances;
        debug_print("Added result from worker " + std::to_string(workerId));
    }

    bool hasAllResults() const {
        return results.size() >= 2;
    }

    void printResults() const {
        for (const auto& [workerId, distances] : results) {
            std::cout << "\nWorker " << workerId << " results:\n";
            std::cout << "Distances: ";
            for (double d : distances) {
                std::cout << d << " ";
            }
            std::cout << "\n";
        }
        std::cout.flush();
    }
};

class Server {
private:
    int serverSocket;
    ResultCollector& collector;

    bool receiveMessage(int clientSocket, std::vector<char>& buffer) {
        Message header;
        ssize_t headerSize = recv(clientSocket, &header, sizeof(header), MSG_WAITALL);
        
        if (headerSize != sizeof(header)) {
            debug_print("Failed to receive header (got " + std::to_string(headerSize) + " bytes)");
            hexDump(&header, headerSize);
            return false;
        }

        debug_print("Received header:");
        hexDump(&header, sizeof(header));

        uint32_t magic = ntohl(header.magic);
        uint32_t size = ntohl(header.size);

        debug_print("Received magic: 0x" + std::to_string(magic));
        debug_print("Expected magic: 0x" + std::to_string(MAGIC_NUMBER));

        if (magic != MAGIC_NUMBER) {
            debug_print("Invalid magic number");
            return false;
        }

        if (size > MAX_MESSAGE_SIZE) {
            debug_print("Message too large: " + std::to_string(size));
            return false;
        }

        debug_print("Expecting " + std::to_string(size) + " bytes of data");
        buffer.resize(size);
        
        ssize_t received = recv(clientSocket, buffer.data(), size, MSG_WAITALL);
        if (received != static_cast<ssize_t>(size)) {
            debug_print("Failed to receive complete data (got " + std::to_string(received) + " bytes)");
            return false;
        }

        debug_print("Received complete data");
        return true;
    }

    void handleClient(int clientSocket) {
        debug_print("New client connection accepted");

        try {
            std::vector<char> buffer;
            if (receiveMessage(clientSocket, buffer)) {
                std::string data(buffer.begin(), buffer.end());
                auto j = json::parse(data);
                int workerId = j["worker_id"];
                std::vector<double> distances = j["distances"];
                collector.addResult(workerId, distances);

                const char* response = "OK";
                send(clientSocket, response, 2, MSG_NOSIGNAL);
                debug_print("Sent OK response to worker " + std::to_string(workerId));
            }
        } catch (const std::exception& e) {
            debug_print("Error: " + std::string(e.what()));
        }

        close(clientSocket);
    }

public:
    Server(ResultCollector& c) : collector(c) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            throw std::runtime_error("Failed to create socket");
        }

        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("Failed to bind socket");
        }

        if (listen(serverSocket, 3) < 0) {
            throw std::runtime_error("Failed to listen");
        }
    }

    void run() {
        debug_print("Master node listening on port " + std::to_string(PORT));

        while (true) {
            sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            
            int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
            if (clientSocket < 0) {
                debug_print("Error accepting connection");
                continue;
            }

            debug_print("Accepted new connection");
            std::thread clientThread(&Server::handleClient, this, clientSocket);
            clientThread.detach();
        }
    }

    ~Server() {
        close(serverSocket);
    }
};

int main() {
    try {
        debug_print("Starting master node");
        ResultCollector collector;
        Server server(collector);

        std::thread serverThread(&Server::run, &server);

        while (!collector.hasAllResults()) {
            debug_print("Waiting for results...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        debug_print("\nAll results received!");
        collector.printResults();

        serverThread.join();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}