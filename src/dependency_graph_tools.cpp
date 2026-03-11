#include "dependency_graph.h"
#include <tiny-mcp/mcp_server.h>

void register_dependency_graph(DependencyGraph& graph, McpToolServer& server) {

    // --- create_node ---

    using CreateParams = std::tuple<
        ToolParam<"id",       "Unique identifier for the node",              std::string>,
        ToolParam<"task",     "Description of what needs to be done",        std::string>,
        ToolParam<"priority", "Priority (higher = more urgent, default 0)",  int>,
        ToolParam<"context",  "Reasoning or background behind this task (optional, empty string if none)", std::string>
    >;

    server.tools().register_tool<CreateParams>(
        "dag_create_node",
        "Create a new node in the dependency graph. "
        "Use dag_add_dependency to wire it to other nodes.",
        [&graph](CreateParams& p) -> std::expected<std::string,std::string> {
            return graph.create_node(
                std::get<0>(p), std::get<1>(p), std::get<2>(p),
                std::get<3>(p));
        }
    );

    // --- create_nodes (batch) ---

    using CreateNodesParams = std::tuple<
        ToolParam<"nodes", "JSON array of objects with fields: id (string), task (string), priority (int, optional), context (string, optional)", std::string>
    >;

    server.tools().register_tool<CreateNodesParams>(
        "dag_create_nodes",
        "Batch-create multiple nodes at once. Pass a JSON array of node objects. "
        "Each object must have 'id' and 'task', optionally 'priority' and 'context'.",
        [&graph](CreateNodesParams& p) -> std::expected<std::string,std::string> {
            std::string input = std::get<0>(p);
            auto j = json::parse(input, nullptr, false);
            if (j.is_discarded() || !j.is_array())
                return std::unexpected("parameter must be a valid JSON array.");
            return graph.create_nodes(j);
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
        [&graph](AddDepParams& p) -> std::expected<std::string,std::string> {
            return graph.add_dependency(
                std::get<0>(p), std::get<1>(p), std::get<2>(p));
        }
    );

    // --- add_dependencies (batch) ---

    using AddDepsParams = std::tuple<
        ToolParam<"dependencies", "JSON array of objects with fields: node_id (string), depends_on (string), rationale (string, optional)", std::string>
    >;

    server.tools().register_tool<AddDepsParams>(
        "dag_add_dependencies",
        "Batch-add multiple dependencies at once. Pass a JSON array of dependency objects. "
        "Each object must have 'node_id' and 'depends_on', optionally 'rationale'.",
        [&graph](AddDepsParams& p) -> std::expected<std::string,std::string> {
            std::string input = std::get<0>(p);
            auto j = json::parse(input, nullptr, false);
            if (j.is_discarded() || !j.is_array())
                return std::unexpected("parameter must be a valid JSON array.");
            return graph.add_dependencies(j);
        }
    );

    // --- next ---

    using EmptyParams = std::tuple<>;

    server.tools().register_tool<EmptyParams>(
        "dag_next",
        "Get the next actionable task from the dependency graph. "
        "Uses priority-aware DFS. Marks the task as in_progress.",
        [&graph](EmptyParams&) -> std::expected<std::string,std::string> {
            return graph.next();
        }
    );

    // --- start ---

    using StartParams = std::tuple<
        ToolParam<"id", "The node to start working on", std::string>
    >;

    server.tools().register_tool<StartParams>(
        "dag_start",
        "Manually start a specific node. Transitions pending/invalidated to "
        "in_progress. Also reopens done nodes (cascades invalidation to dependents). "
        "Use this to override dag_next's ordering or to reopen completed work.",
        [&graph](StartParams& p) -> std::expected<std::string,std::string> {
            return graph.start(std::get<0>(p));
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
        [&graph](DoneParams& p) -> std::expected<std::string,std::string> {
            return graph.done(std::get<0>(p), std::get<1>(p));
        }
    );

    // --- done_batch ---

    using DoneBatchParams = std::tuple<
        ToolParam<"items", "JSON array of objects with fields: id (string), summary (string)", std::string>
    >;

    server.tools().register_tool<DoneBatchParams>(
        "dag_done_batch",
        "Mark multiple in_progress tasks as done at once. "
        "Pass a JSON array of objects with 'id' and 'summary'.",
        [&graph](DoneBatchParams& p) -> std::expected<std::string,std::string> {
            std::string input = std::get<0>(p);
            auto j = json::parse(input, nullptr, false);
            if (j.is_discarded() || !j.is_array())
                return std::unexpected("parameter must be a valid JSON array.");
            return graph.done_batch(j);
        }
    );

    // --- next_batch ---

    using NextBatchParams = std::tuple<
        ToolParam<"n", "Maximum number of actionable tasks to return (0 = all)", int>
    >;

    server.tools().register_tool<NextBatchParams>(
        "dag_next_batch",
        "List up to n actionable tasks ranked by effective priority. "
        "Does NOT change node state — use dag_start to begin working on a task.",
        [&graph](NextBatchParams& p) -> std::expected<std::string,std::string> {
            return graph.next_batch(std::get<0>(p));
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
        [&graph](DeleteParams& p) -> std::expected<std::string,std::string> {
            return graph.delete_node(std::get<0>(p), std::get<1>(p));
        }
    );

    // --- undelete ---

    using UndeleteParams = std::tuple<
        ToolParam<"id", "The node to restore", std::string>
    >;

    server.tools().register_tool<UndeleteParams>(
        "dag_undelete",
        "Restore a deleted node to its pre-deletion state.",
        [&graph](UndeleteParams& p) -> std::expected<std::string,std::string> {
            return graph.undelete(std::get<0>(p));
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
        [&graph](LogParams& p) -> std::expected<std::string,std::string> {
            return graph.log(std::get<0>(p), std::get<1>(p));
        }
    );

    // --- status ---

    server.tools().register_tool<EmptyParams>(
        "dag_status",
        "Overview of all non-deleted nodes grouped by state, "
        "with priority and dependency progress.",
        [&graph](EmptyParams&) -> std::expected<std::string,std::string> {
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
        [&graph](ShowParams& p) -> std::expected<std::string,std::string> {
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
        [&graph](SaveParams& p) -> std::expected<std::string,std::string> {
            return graph.save(std::get<0>(p), std::get<1>(p));
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
        [&graph](LoadParams& p) -> std::expected<std::string,std::string> {
            return graph.load(std::get<0>(p));
        }
    );

    // --- usage_guide ---

    server.tools().register_tool<EmptyParams>(
        "dag_usage_guide",
        "Returns a comprehensive guide on how to use the dependency graph tools effectively. "
        "Call this at the start of a session to understand the principles of operation.",
        [](EmptyParams&) -> std::expected<std::string,std::string> {
            return DependencyGraph::usage_guide();
        }
    );
}
