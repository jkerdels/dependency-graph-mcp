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

# --- Core library (shared by server and tests) ---
add_library(dep_graph_lib STATIC
    src/dependency_graph.cpp
)
target_link_libraries(dep_graph_lib PUBLIC tiny-mcp::tiny-mcp)
target_include_directories(dep_graph_lib PUBLIC src/)

# --- Server executable ---
add_executable(dependency-graph-mcp
    src/main.cpp
    src/dependency_graph_tools.cpp
)

target_link_libraries(dependency-graph-mcp PRIVATE dep_graph_lib)

# --- Tests ---
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
FetchContent_MakeAvailable(doctest)

enable_testing()
add_subdirectory(tests)
