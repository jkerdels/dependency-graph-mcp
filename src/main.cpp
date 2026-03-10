#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <tiny-mcp/mcp_server.h>
#include "dependency_graph.h"

void register_dependency_graph(DependencyGraph&, McpToolServer&);

static std::atomic<bool> shutdown_requested{false};

static void signal_handler(int) {
    shutdown_requested.store(true, std::memory_order_relaxed);
}

int main() {
    std::ios_base::sync_with_stdio(false);

    // Handle termination signals gracefully so we exit 0 instead of 128+sig
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    // Ignore SIGPIPE so writing to closed pipes doesn't kill us
    std::signal(SIGPIPE, SIG_IGN);

    McpToolServer server("dependency-graph-mcp", "0.1.0");

    DependencyGraph dep_graph;
    register_dependency_graph(dep_graph, server);

    std::cerr << "[mcp] server started, waiting for messages on stdin..." << std::endl;

    json message;
    try {
        while (!shutdown_requested.load(std::memory_order_relaxed) && std::cin >> message) {
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

    if (std::cin.bad()) {
        std::cerr << "[mcp] stdin read error." << std::endl;
        return 1;
    }

    std::cerr << "[mcp] shutting down." << std::endl;
    return 0;
}
