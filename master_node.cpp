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
#include "common.h"

using json = nlohmann::json;

class ResultCollector {
private:
    std::map<int, std::vector<double>> results;
    std::mutex mtx;

public:
    void addResult(int workerId, const std::vector<double>& distances) {
        std::lock_guard<std::mutex> lock(mtx);
        results[workerId] = distances;
        std::cout << "[Master] Received results from Worker " << workerId << "\n";
    }

    bool hasAllResults(size_t expectedResults) const {
        return results.size() >= expectedResults;
    }

    void printResults() const {
        for (const auto& [workerId, distances] : results) {
            std::cout << "\nWorker " << workerId << " results:\n";
            for (double d : distances) {
                std::cout << d << " ";
            }
            std::cout << "\n";
        }
    }
};

class Server {
private:
    int serverSocket;
    ResultCollector& collector;

    void handleClient(int clientSocket) {
        try {
            Message header;
            ssize_t received = recv(clientSocket, &header, sizeof(header), MSG_WAITALL);

            if (received != sizeof(header) || ntohl(header.magic) != MAGIC_NUMBER) {
                std::cerr << "[Master] Invalid header received.\n";
                close(clientSocket);
                return;
            }

            uint32_t size = ntohl(header.size);
            if (size > MAX_MESSAGE_SIZE) {
                std::cerr << "[Master] Message too large.\n";
                close(clientSocket);
                return;
            }

            std::vector<char> buffer(size);
            received = recv(clientSocket, buffer.data(), size, MSG_WAITALL);
            if (received != size) {
                std::cerr << "[Master] Incomplete data received.\n";
                close(clientSocket);
                return;
            }

            auto j = json::parse(buffer.begin(), buffer.end());
            int workerId = j["worker_id"];
            std::vector<double> distances = j["distances"];
            collector.addResult(workerId, distances);

            send(clientSocket, "OK", 2, MSG_NOSIGNAL);
        } catch (const std::exception& e) {
            std::cerr << "[Master] Error: " << e.what() << "\n";
        }
        close(clientSocket);
    }

public:
    Server(ResultCollector& c) : collector(c) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        bind(serverSocket, (struct sockaddr*)&address, sizeof(address));
        listen(serverSocket, 3);
    }

    void run() {
        std::cout << "[Master] Listening on port " << PORT << "\n";
        while (true) {
            sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
            if (clientSocket >= 0) {
                std::thread(&Server::handleClient, this, clientSocket).detach();
            }
        }
    }

    ~Server() {
        close(serverSocket);
    }
};

int main() {
    try {
        ResultCollector collector;
        Server server(collector);
        std::thread serverThread(&Server::run, &server);

        while (!collector.hasAllResults(2)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        collector.printResults();
        serverThread.join();
    } catch (const std::exception& e) {
        std::cerr << "[Master] Error: " << e.what() << "\n";
    }
    return 0;
}
