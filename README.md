# dependency-graph-mcp

An MCP tool server that provides a priority-aware dependency graph (DAG) for task management. Built with [tiny-mcp](https://github.com/jkerdels/tiny-mcp).

## What it does

This server gives AI assistants (or any MCP client) a structured way to plan and execute tasks with dependencies. Tasks are organized as a directed acyclic graph where:

- Each node represents a task with a priority and optional context (design rationale)
- Edges represent "depends on" relationships
- Cycle detection prevents invalid dependency chains
- Effective priority reflects the highest-priority *unfinished* dependency — once a blocker is resolved, it stops inflating downstream priorities
- State changes cascade automatically (e.g., reopening a completed task invalidates its dependents)

## Tools

| Tool | Description |
|------|-------------|
| `dag_create_node` | Create a task node with id, description, priority, and optional context |
| `dag_create_nodes` | Batch-create multiple nodes from a JSON array |
| `dag_add_dependency` | Add a dependency between existing nodes |
| `dag_add_dependencies` | Batch-add multiple dependencies from a JSON array |
| `dag_next` | Get the next actionable task (priority-aware DFS), auto-marks as in_progress |
| `dag_next_batch` | List up to n actionable tasks ranked by priority (does not auto-start) |
| `dag_start` | Manually start a specific node, or reopen a completed one (cascades invalidation) |
| `dag_done` | Mark a task as completed |
| `dag_done_batch` | Mark multiple in_progress tasks as done at once |
| `dag_delete_node` | Soft-delete a node (preserves data, supports undo) |
| `dag_undelete` | Restore a deleted node |
| `dag_log` | Append a note to a node's changelog |
| `dag_status` | Overview of all nodes grouped by state, with progress percentage |
| `dag_show` | Display the graph structure (text or JSON) |
| `dag_save` | Save graph to JSON file (with optional auto-save) |
| `dag_load` | Load graph from JSON file |
| `dag_usage_guide` | Returns a comprehensive guide on principles of operation |

## Task states

```
pending → in_progress → done
    ↑         ↑           |
    |         +-----------+  (via dag_start — reopens, cascades invalidation)
    |                     |
    +--- invalidated ←----+  (triggered by new dependencies or reopened deps)

deleted (soft, restorable via undelete)
```

## Key concepts

- **Priority (p)**: user-assigned importance (higher = more urgent).
- **Effective priority (ep)**: the maximum priority among the node and its *unfinished* transitive dependencies. Reflects "how hot is my current bottleneck" — once a dependency is done, it stops contributing to ep.
- **Context**: reasoning or background behind a task, set at creation time. Use this for *why* a task exists. Use `dag_log` for *what happened* during execution.
- **Cascade invalidation**: when a completed node gains a new dependency or is reopened via `dag_start`, it and all its transitive dependents are marked invalidated, signaling they need re-verification.

## Building

```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

CMake will automatically fetch [tiny-mcp](https://github.com/jkerdels/tiny-mcp) and [nlohmann/json](https://github.com/nlohmann/json) via `FetchContent`.

## Usage

Register the server with Claude Code using the CLI:

```bash
claude mcp add --scope user --transport stdio dependency-graph -- /path/to/build/dependency-graph-mcp
```

This makes the server available across all your projects. Use `--scope project` instead to share it with your team via the project's `.mcp.json`.

You can verify it's registered by typing `/mcp` inside Claude Code.

## Requirements

- C++23
- CMake 3.14+
