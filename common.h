#pragma once

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <limits>

// Constants
const uint32_t MAGIC_NUMBER = 0x12345678;
const int PORT = 12345;
const size_t MAX_MESSAGE_SIZE = 1024 * 1024;

// Message structure
struct Message {
    uint32_t magic;
    uint32_t size;
} __attribute__((packed));

// Utility functions
inline void printHex(const void* data, size_t size) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(p[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

inline void printGraph(const std::vector<std::vector<int>>& graph) {
    std::cout << "Graph adjacency matrix:" << std::endl;
    for (const auto& row : graph) {
        for (int val : row) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
}

inline void printDistances(const std::vector<double>& distances) {
    std::cout << "Distances from start node:" << std::endl;
    for (size_t i = 0; i < distances.size(); i++) {
        if (distances[i] == std::numeric_limits<double>::infinity()) {
            std::cout << i << ": INF" << std::endl;
        } else {
            std::cout << i << ": " << distances[i] << std::endl;
        }
    }
}
