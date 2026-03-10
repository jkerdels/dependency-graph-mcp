#include <iostream>
#include <string>
#include <tiny-mcp/mcp_server.h>
#include "dependency_graph.h"

void register_dependency_graph(DependencyGraph&, McpToolServer&);

int main() {
    std::ios_base::sync_with_stdio(false);

    McpToolServer server("dependency-graph-mcp", "0.1.0");

    DependencyGraph dep_graph;
    register_dependency_graph(dep_graph, server);

    std::cerr << "[mcp] server started, waiting for messages on stdin..." << std::endl;

    json message;
    try {
        while (std::cin >> message) {
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

    std::cerr << "[mcp] stdin closed, shutting down." << std::endl;
    return 0;
}
