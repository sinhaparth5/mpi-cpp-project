cmake_minimum_required(VERSION 3.10)
project(distributed_graph)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find MPI package
find_package(MPI REQUIRED)

# Add JSON library
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Common header directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Master node executable
add_executable(master_node master_node.cpp)
target_link_libraries(master_node PRIVATE 
    pthread
    nlohmann_json::nlohmann_json
)

# Worker node executable
add_executable(worker_node worker_node.cpp)
target_link_libraries(worker_node PRIVATE 
    MPI::MPI_CXX
    nlohmann_json::nlohmann_json
)