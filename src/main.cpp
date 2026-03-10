#include <iostream>
#include <string>
#include <csignal>
#include <tiny-mcp/mcp_server.h>
#include "dependency_graph.h"

void register_dependency_graph(DependencyGraph&, McpToolServer&);

static volatile std::sig_atomic_t shutdown_requested = 0;

static void signal_handler(int) {
    shutdown_requested = 1;
}

int main() {
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    McpToolServer server("dependency-graph-mcp", "0.1.0");

    DependencyGraph dep_graph;
    register_dependency_graph(dep_graph, server);

    std::cerr << "[mcp] server started, waiting for messages on stdin..." << std::endl;

    json message;
    try {
        while (!shutdown_requested && std::cin >> message) {
            if (shutdown_requested) break;

            std::cerr << "[mcp] recv: " << message << std::endl;

            auto response = server.handle_message(message);

            if (response) {
                std::cerr << "[mcp] send: " << *response << std::endl;
                std::cout << *response << "\n";
                std::cout.flush();
            }
        }
    } catch (const json::parse_error&) {
        // nlohmann::json throws parse_error on EOF instead of setting eofbit
    }

    std::cerr << "[mcp] shutting down." << std::endl;
    return 0;
}
