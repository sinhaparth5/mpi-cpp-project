#include <mpi.h>
#include <iostream>
#include <vector>
#include <limits>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <netdb.h>
#include <iomanip>
#include <netinet/tcp.h>
#include "common.h"

using json = nlohmann::json;

void debug_print(const std::string& msg, bool force_flush = false) {
    static std::mutex print_mutex;
    std::lock_guard<std::mutex> lock(print_mutex);
    std::cout << "[Worker] " << msg << std::endl;
    if (force_flush) std::cout.flush();
}

bool configureSocket(int sock) {
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        debug_print("Failed to set SO_REUSEADDR");
        return false;
    }

    // Enable TCP keepalive
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        debug_print("Failed to set SO_KEEPALIVE");
        return false;
    }

    // Set TCP keepalive parameters
    int keepalive_time = 1;  // Start probing after 1 second of idle
    int keepalive_intvl = 1; // Probe interval of 1 second
    int keepalive_probes = 5; // 5 probes before giving up

    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time, sizeof(keepalive_time));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl, sizeof(keepalive_intvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes, sizeof(keepalive_probes));

    return true;
}

bool sendResultToMaster(int workerId, const std::vector<double>& distances, int attempt = 1) {
    debug_print("Attempt " + std::to_string(attempt) + " to send results to master", true);

    // Add delay for retries
    if (attempt > 1) {
        std::this_thread::sleep_for(std::chrono::seconds(attempt * 2));
    }

    struct hostent* he = gethostbyname("master");
    if (!he) {
        debug_print("Failed to resolve master hostname", true);
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        debug_print("Failed to create socket", true);
        return false;
    }

    if (!configureSocket(sock)) {
        close(sock);
        return false;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);

    debug_print("Attempting to connect to master...", true);
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        debug_print("Connection failed: " + std::string(strerror(errno)), true);
        close(sock);
        return false;
    }

    debug_print("Connected to master", true);

    try {
        Message msg;
        msg.magic = htonl(MAGIC_NUMBER);
        
        json j;
        j["worker_id"] = workerId;
        j["distances"] = distances;
        std::string data = j.dump();
        
        msg.size = htonl(static_cast<uint32_t>(data.length()));

        debug_print("Sending header with magic: 0x" + 
                   std::to_string(ntohl(msg.magic)), true);

        // Send header
        if (send(sock, &msg, sizeof(msg), MSG_NOSIGNAL) != sizeof(msg)) {
            debug_print("Failed to send header", true);
            close(sock);
            return false;
        }

        // Send data
        if (send(sock, data.c_str(), data.length(), MSG_NOSIGNAL) != 
            static_cast<ssize_t>(data.length())) {
            debug_print("Failed to send data", true);
            close(sock);
            return false;
        }

        debug_print("Waiting for response...", true);
        
        char response[16];
        ssize_t received = recv(sock, response, 2, 0);
        if (received == 2) {
            response[2] = '\0';
            std::string resp(response);
            debug_print("Received response: " + resp, true);
            close(sock);
            return resp == "OK";
        }
        
        debug_print("No response received", true);
    } catch (const std::exception& e) {
        debug_print("Error: " + std::string(e.what()), true);
    }

    close(sock);
    return false;
}

std::vector<std::vector<int>> getGraphForWorker(int workerId) {
    if (workerId == 1) {
        return {
            {0, 1, 0, 0, 1},
            {1, 0, 1, 0, 0},
            {0, 1, 0, 1, 0},
            {0, 0, 1, 0, 1},
            {1, 0, 0, 1, 0}
        };
    } else {
        return {
            {0, 1, 1, 0, 0, 0},
            {1, 0, 0, 1, 0, 0},
            {1, 0, 0, 0, 1, 0},
            {0, 1, 0, 0, 0, 1},
            {0, 0, 1, 0, 0, 1},
            {0, 0, 0, 1, 1, 0}
        };
    }
}

std::vector<double> parallelBFS(const std::vector<std::vector<int>>& graph, int startNode) {
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    debug_print("Starting BFS computation with rank " + std::to_string(rank));

    int n = graph.size();
    std::vector<double> distances(n, std::numeric_limits<double>::infinity());
    
    if (rank == 0) {
        distances[startNode] = 0;
    }

    MPI_Bcast(distances.data(), n, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    bool changed;
    do {
        changed = false;
        for (int i = rank; i < n; i += size) {
            for (int j = 0; j < n; j++) {
                if (graph[i][j] == 1 && distances[j] > distances[i] + 1) {
                    distances[j] = distances[i] + 1;
                    changed = true;
                }
            }
        }

        bool globalChanged;
        MPI_Allreduce(&changed, &globalChanged, 1, MPI_C_BOOL, MPI_LOR, MPI_COMM_WORLD);
        changed = globalChanged;

        MPI_Bcast(distances.data(), n, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    } while (changed);

    debug_print("BFS computation completed for rank " + std::to_string(rank));
    return distances;
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc != 2) {
        if (rank == 0) {
            std::cerr << "Usage: " << argv[0] << " <worker_id>" << std::endl;
        }
        MPI_Finalize();
        return 1;
    }

    int workerId = std::stoi(argv[1]);
    
    if (rank == 0) {
        debug_print("Worker " + std::to_string(workerId) + " starting computation...", true);
    }
    
    auto graph = getGraphForWorker(workerId);
    auto distances = parallelBFS(graph, 0);

    if (rank == 0) {
        debug_print("Worker " + std::to_string(workerId) + " finished computation", true);
        
        // Print computed distances
        debug_print("Computed distances:", true);
        for (size_t i = 0; i < distances.size(); i++) {
            debug_print("  Node " + std::to_string(i) + ": " + 
                       std::to_string(distances[i]), true);
        }

        for (int attempt = 1; attempt <= 3; attempt++) {
            debug_print("Attempt " + std::to_string(attempt) + " to send results", true);
            if (sendResultToMaster(workerId, distances)) {
                debug_print("Successfully sent results to master", true);
                break;
            }
            if (attempt < 3) {
                std::this_thread::sleep_for(std::chrono::seconds(attempt * 2));
            }
        }
    }

    MPI_Finalize();
    return 0;
}