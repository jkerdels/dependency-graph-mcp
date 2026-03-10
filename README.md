# dependency-graph-mcp

An MCP tool server that provides a priority-aware dependency graph (DAG) for task management. Built with [tiny-mcp](https://github.com/jkerdels/tiny-mcp).

## What it does

This server gives AI assistants (or any MCP client) a structured way to plan and execute tasks with dependencies. Tasks are organized as a directed acyclic graph where:

- Each node represents a task with a priority
- Edges represent "depends on" relationships
- Cycle detection prevents invalid dependency chains
- Priority propagation ensures high-priority work surfaces first
- State changes cascade automatically (e.g., adding a dependency to a completed task invalidates it)

## Tools

| Tool | Description |
|------|-------------|
| `dag_create_node` | Create a task node, optionally as a dependency of a parent |
| `dag_add_dependency` | Add a dependency between existing nodes |
| `dag_next` | Get the next actionable task (priority-aware DFS) |
| `dag_done` | Mark a task as completed |
| `dag_delete_node` | Soft-delete a node (preserves data, supports undo) |
| `dag_undelete` | Restore a deleted node |
| `dag_log` | Append a note to a node's changelog |
| `dag_status` | Overview of all nodes grouped by state |
| `dag_show` | Display the graph structure (text or JSON) |
| `dag_save` | Save graph to JSON file (with optional auto-save) |
| `dag_load` | Load graph from JSON file |

## Task states

```
pending → in_progress → done
    ↑                     |
    +--- invalidated ←----+  (triggered by new dependencies)

deleted (soft, restorable via undelete)
```

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
