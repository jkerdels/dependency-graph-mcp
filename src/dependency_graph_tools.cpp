#include "dependency_graph.h"
#include <tiny-mcp/mcp_server.h>

void register_dependency_graph(DependencyGraph& graph, McpToolServer& server) {

    // --- create_node ---

    using CreateParams = std::tuple<
        ToolParam<"id",        "Unique identifier for the node",          std::string>,
        ToolParam<"task",      "Description of what needs to be done",    std::string>,
        ToolParam<"priority",  "Priority (higher = more urgent, default 0)", int>,
        ToolParam<"parent_id", "Parent node that will depend on this node (optional, empty string if none)", std::string>,
        ToolParam<"rationale", "Why the parent depends on this node (optional)", std::string>
    >;

    server.tools().register_tool<CreateParams>(
        "dag_create_node",
        "Create a new node in the dependency graph. Optionally attach it as a "
        "dependency of an existing parent node.",
        [&graph](CreateParams& p) -> std::string {
            auto result = graph.create_node(
                std::get<0>(p), std::get<1>(p), std::get<2>(p),
                std::get<3>(p), std::get<4>(p));
            return result ? *result : result.error();
        }
    );

    // --- add_dependency ---

    using AddDepParams = std::tuple<
        ToolParam<"node_id",    "The node that will gain a dependency",    std::string>,
        ToolParam<"depends_on", "The node it will depend on",             std::string>,
        ToolParam<"rationale",  "Why this dependency exists",             std::string>
    >;

    server.tools().register_tool<AddDepParams>(
        "dag_add_dependency",
        "Add a dependency: node_id will depend on depends_on. "
        "Rejects cycles. May cascade invalidation.",
        [&graph](AddDepParams& p) -> std::string {
            auto result = graph.add_dependency(
                std::get<0>(p), std::get<1>(p), std::get<2>(p));
            return result ? *result : result.error();
        }
    );

    // --- next ---

    using EmptyParams = std::tuple<>;

    server.tools().register_tool<EmptyParams>(
        "dag_next",
        "Get the next actionable task from the dependency graph. "
        "Uses priority-aware DFS. Marks the task as in_progress.",
        [&graph](EmptyParams&) -> std::string {
            auto result = graph.next();
            return result ? *result : result.error();
        }
    );

    // --- done ---

    using DoneParams = std::tuple<
        ToolParam<"id",      "The node to mark as done",       std::string>,
        ToolParam<"summary", "Summary of what was accomplished", std::string>
    >;

    server.tools().register_tool<DoneParams>(
        "dag_done",
        "Mark a task as done. Must be in_progress.",
        [&graph](DoneParams& p) -> std::string {
            auto result = graph.done(std::get<0>(p), std::get<1>(p));
            return result ? *result : result.error();
        }
    );

    // --- delete_node ---

    using DeleteParams = std::tuple<
        ToolParam<"id",     "The node to delete",       std::string>,
        ToolParam<"reason", "Why this node is deleted",  std::string>
    >;

    server.tools().register_tool<DeleteParams>(
        "dag_delete_node",
        "Soft-delete a node. Preserves data for reasoning and undo. "
        "Deleted dependencies are treated as met.",
        [&graph](DeleteParams& p) -> std::string {
            auto result = graph.delete_node(std::get<0>(p), std::get<1>(p));
            return result ? *result : result.error();
        }
    );

    // --- undelete ---

    using UndeleteParams = std::tuple<
        ToolParam<"id", "The node to restore", std::string>
    >;

    server.tools().register_tool<UndeleteParams>(
        "dag_undelete",
        "Restore a deleted node to its pre-deletion state.",
        [&graph](UndeleteParams& p) -> std::string {
            auto result = graph.undelete(std::get<0>(p));
            return result ? *result : result.error();
        }
    );

    // --- log ---

    using LogParams = std::tuple<
        ToolParam<"id",    "The node to add a log entry to", std::string>,
        ToolParam<"entry", "The log entry text",             std::string>
    >;

    server.tools().register_tool<LogParams>(
        "dag_log",
        "Append a changelog entry to a node without changing its state.",
        [&graph](LogParams& p) -> std::string {
            auto result = graph.log(std::get<0>(p), std::get<1>(p));
            return result ? *result : result.error();
        }
    );

    // --- status ---

    server.tools().register_tool<EmptyParams>(
        "dag_status",
        "Overview of all non-deleted nodes grouped by state, "
        "with priority and dependency progress.",
        [&graph](EmptyParams&) -> std::string {
            return graph.status();
        }
    );

    // --- show ---

    using ShowParams = std::tuple<
        ToolParam<"id",     "Node to show sub-DAG for (empty for full graph)", std::string>,
        ToolParam<"format", "Output format: 'text' (default) or 'json'",      std::string>
    >;

    server.tools().register_tool<ShowParams>(
        "dag_show",
        "Show the dependency graph. If id is given, shows the sub-DAG of "
        "all transitive prerequisites. Format: 'text' or 'json'.",
        [&graph](ShowParams& p) -> std::string {
            return graph.show(std::get<0>(p), std::get<1>(p));
        }
    );

    // --- save ---

    using SaveParams = std::tuple<
        ToolParam<"filepath",  "Path to save the graph JSON to",          std::string>,
        ToolParam<"auto_save", "Enable auto-save on every mutation (true/false)", bool>
    >;

    server.tools().register_tool<SaveParams>(
        "dag_save",
        "Save the graph to a JSON file. Optionally enable auto-save mode.",
        [&graph](SaveParams& p) -> std::string {
            auto result = graph.save(std::get<0>(p), std::get<1>(p));
            return result ? *result : result.error();
        }
    );

    // --- load ---

    using LoadParams = std::tuple<
        ToolParam<"filepath", "Path to load graph JSON from", std::string>
    >;

    server.tools().register_tool<LoadParams>(
        "dag_load",
        "Load a graph from a JSON file. Replaces current graph. "
        "Auto-save is NOT inherited.",
        [&graph](LoadParams& p) -> std::string {
            auto result = graph.load(std::get<0>(p));
            return result ? *result : result.error();
        }
    );
}
