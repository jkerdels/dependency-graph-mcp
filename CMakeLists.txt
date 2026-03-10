cmake_minimum_required(VERSION 3.14)
project(dependency-graph-mcp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---
include(FetchContent)

FetchContent_Declare(
    tiny_mcp
    GIT_REPOSITORY https://github.com/jkerdels/tiny-mcp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(tiny_mcp)

# --- Server executable ---
add_executable(dependency-graph-mcp
    src/main.cpp
    src/dependency_graph.cpp
    src/dependency_graph_tools.cpp
)

target_link_libraries(dependency-graph-mcp PRIVATE tiny-mcp::tiny-mcp)
