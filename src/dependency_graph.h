#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <expected>
#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DependencyGraph {
public:
    enum class State { pending, in_progress, done, invalidated, deleted };

    struct Dependency {
        std::string node_id;
        std::string rationale;
    };

    struct ChangelogEntry {
        std::string timestamp;
        std::string entry;
    };

    struct Node {
        std::string id;
        std::string task;
        std::string context;  // reasoning / background behind the task
        State       state = State::pending;
        int         priority = 0;
        int         effective_priority = 0;
        State       pre_delete_state = State::pending; // for undelete
        std::vector<Dependency>     dependencies;
        std::vector<ChangelogEntry> changelog;
    };

    // --- Construction ---

    std::expected<std::string, std::string>
    create_node(const std::string& id,
                const std::string& task,
                int priority = 0,
                const std::string& context = "");

    std::expected<std::string, std::string>
    add_dependency(const std::string& node_id,
                   const std::string& depends_on,
                   const std::string& rationale);

    // --- Working ---

    std::expected<std::string, std::string> next();

    std::expected<std::string, std::string>
    start(const std::string& id);

    std::expected<std::string, std::string>
    done(const std::string& id, const std::string& summary);

    std::expected<std::string, std::string>
    delete_node(const std::string& id, const std::string& reason);

    std::expected<std::string, std::string>
    undelete(const std::string& id);

    // --- Information ---

    std::expected<std::string, std::string>
    log(const std::string& id, const std::string& entry);

    std::string status();

    std::string show(const std::string& id = "", const std::string& format = "text");

    static std::string usage_guide();

    // --- Persistence ---

    std::expected<std::string, std::string>
    save(const std::string& filepath, bool auto_save = false);

    std::expected<std::string, std::string>
    load(const std::string& filepath);

private:
    std::map<std::string, Node> nodes_;
    bool priorities_dirty_ = false;

    // Auto-save state
    std::string auto_save_path_;
    bool auto_save_enabled_ = false;

    // --- Helpers ---

    static std::string now_iso8601();

    void append_changelog(Node& node, const std::string& entry);

    // Cycle detection: is target reachable from source via dependency edges?
    bool is_reachable(const std::string& source, const std::string& target) const;

    // Cascade invalidation upward from a node
    void cascade_invalidate(const std::string& from_id, const std::string& reason);

    // Find all nodes that depend on the given node (reverse edges)
    std::vector<std::string> dependents_of(const std::string& id) const;

    // Recompute effective priorities (bottom-up topological pass)
    void recompute_priorities();

    // Check if a node is actionable (pending/invalidated, all deps met)
    bool is_actionable(const Node& node) const;

    // DFS for next actionable node, priority-aware
    std::optional<std::string> dfs_next(const std::string& id);

    // Collect the sub-DAG reachable from a node
    void collect_subdag(const std::string& id,
                        std::map<std::string, const Node*>& visited) const;

    // Formatting
    std::string format_text(const std::map<std::string, const Node*>& nodes) const;
    std::string format_json(const std::map<std::string, const Node*>& nodes) const;

    static std::string state_to_string(State s);
    static State string_to_state(const std::string& s);
    static std::string state_indicator(State s);

    void auto_save_if_enabled();

    json to_json() const;
    void from_json(const json& j);
};
