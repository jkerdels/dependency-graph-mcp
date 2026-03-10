#include "dependency_graph.h"

#include <algorithm>
#include <sstream>
#include <queue>
#include <set>
#include <iomanip>

// ============================================================================
// HELPERS
// ============================================================================

std::string DependencyGraph::now_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time), "%FT%TZ");
    return ss.str();
}

void DependencyGraph::append_changelog(Node& node, const std::string& entry) {
    node.changelog.push_back({now_iso8601(), entry});
}

std::string DependencyGraph::state_to_string(State s) {
    switch (s) {
        case State::pending:     return "pending";
        case State::in_progress: return "in_progress";
        case State::done:        return "done";
        case State::invalidated: return "invalidated";
        case State::deleted:     return "deleted";
    }
    return "unknown";
}

DependencyGraph::State DependencyGraph::string_to_state(const std::string& s) {
    if (s == "pending")     return State::pending;
    if (s == "in_progress") return State::in_progress;
    if (s == "done")        return State::done;
    if (s == "invalidated") return State::invalidated;
    if (s == "deleted")     return State::deleted;
    return State::pending;
}

std::string DependencyGraph::state_indicator(State s) {
    switch (s) {
        case State::pending:     return "[pending]";
        case State::in_progress: return "[in_progress]";
        case State::done:        return "[done]";
        case State::invalidated: return "[!!!]";
        case State::deleted:     return "[DEL]";
    }
    return "[?]";
}

bool DependencyGraph::is_reachable(const std::string& source,
                                   const std::string& target) const {
    // BFS from source following dependency edges
    std::set<std::string> visited;
    std::queue<std::string> q;
    q.push(source);

    while (!q.empty()) {
        auto current = q.front();
        q.pop();

        if (current == target) return true;
        if (visited.count(current)) continue;
        visited.insert(current);

        auto it = nodes_.find(current);
        if (it == nodes_.end()) continue;

        for (const auto& dep : it->second.dependencies) {
            q.push(dep.node_id);
        }
    }
    return false;
}

std::vector<std::string> DependencyGraph::dependents_of(const std::string& id) const {
    std::vector<std::string> result;
    for (const auto& [nid, node] : nodes_) {
        for (const auto& dep : node.dependencies) {
            if (dep.node_id == id) {
                result.push_back(nid);
                break;
            }
        }
    }
    return result;
}

void DependencyGraph::cascade_invalidate(const std::string& from_id,
                                         const std::string& reason) {
    std::queue<std::string> q;
    q.push(from_id);

    while (!q.empty()) {
        auto current = q.front();
        q.pop();

        auto it = nodes_.find(current);
        if (it == nodes_.end()) continue;

        auto& node = it->second;
        if (node.state == State::done || node.state == State::in_progress) {
            node.state = State::invalidated;
            append_changelog(node, "Invalidated: " + reason);

            for (const auto& dep_id : dependents_of(current)) {
                q.push(dep_id);
            }
        }
    }
}

bool DependencyGraph::is_actionable(const Node& node) const {
    if (node.state != State::pending && node.state != State::invalidated)
        return false;

    for (const auto& dep : node.dependencies) {
        auto it = nodes_.find(dep.node_id);
        if (it == nodes_.end()) continue;
        // Deleted deps are treated as met
        if (it->second.state == State::deleted) continue;
        if (it->second.state != State::done) return false;
    }
    return true;
}

// ============================================================================
// PRIORITY
// ============================================================================

void DependencyGraph::recompute_priorities() {
    if (!priorities_dirty_) return;

    // Topological sort via Kahn's algorithm, then compute bottom-up
    std::map<std::string, int> in_degree;
    for (const auto& [id, node] : nodes_) {
        if (!in_degree.count(id)) in_degree[id] = 0;
        for (const auto& dep : node.dependencies) {
            // dep.node_id is a prerequisite of id
            // For topological order we need: process deps before dependents
            in_degree[id]++; // id depends on dep.node_id
        }
    }

    // Start with leaves (no dependencies / in_degree == 0)
    std::queue<std::string> q;
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) q.push(id);
    }

    // Process in topological order (leaves first)
    std::vector<std::string> topo_order;
    while (!q.empty()) {
        auto current = q.front();
        q.pop();
        topo_order.push_back(current);

        // For each node that depends on current, decrease its in_degree
        for (const auto& dep_id : dependents_of(current)) {
            in_degree[dep_id]--;
            if (in_degree[dep_id] == 0) q.push(dep_id);
        }
    }

    // Bottom-up: leaves are processed first, so when we reach a node,
    // all its dependencies already have their effective_priority computed
    for (const auto& id : topo_order) {
        auto& node = nodes_[id];
        node.effective_priority = node.priority;
        for (const auto& dep : node.dependencies) {
            auto it = nodes_.find(dep.node_id);
            if (it != nodes_.end()) {
                node.effective_priority = std::max(
                    node.effective_priority, it->second.effective_priority);
            }
        }
    }

    priorities_dirty_ = false;
}

// ============================================================================
// DFS NEXT
// ============================================================================

std::optional<std::string> DependencyGraph::dfs_next(const std::string& id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return std::nullopt;

    auto& node = it->second;
    if (node.state == State::deleted) return std::nullopt;
    if (node.state == State::done) return std::nullopt;

    // If this node is actionable, it's a candidate
    if (is_actionable(node)) return id;

    // Otherwise, recurse into dependencies, sorted by effective_priority desc
    auto deps = node.dependencies;
    std::sort(deps.begin(), deps.end(),
        [this](const Dependency& a, const Dependency& b) {
            auto ia = nodes_.find(a.node_id);
            auto ib = nodes_.find(b.node_id);
            int pa = (ia != nodes_.end()) ? ia->second.effective_priority : 0;
            int pb = (ib != nodes_.end()) ? ib->second.effective_priority : 0;
            return pa > pb;
        });

    for (const auto& dep : deps) {
        auto result = dfs_next(dep.node_id);
        if (result) return result;
    }

    return std::nullopt;
}

// ============================================================================
// GRAPH CONSTRUCTION
// ============================================================================

std::expected<std::string, std::string>
DependencyGraph::create_node(const std::string& id,
                             const std::string& task,
                             int priority,
                             const std::string& parent_id,
                             const std::string& rationale) {
    if (nodes_.count(id)) {
        return std::unexpected("Node '" + id + "' already exists.");
    }

    if (!parent_id.empty() && !nodes_.count(parent_id)) {
        return std::unexpected("Parent node '" + parent_id + "' not found.");
    }

    Node node;
    node.id = id;
    node.task = task;
    node.priority = priority;
    node.effective_priority = priority;
    append_changelog(node, "Node created");

    nodes_[id] = std::move(node);
    priorities_dirty_ = true;

    // If parent specified, add dependency: parent depends on this new node
    if (!parent_id.empty()) {
        auto& parent = nodes_[parent_id];
        parent.dependencies.push_back({id, rationale});
        append_changelog(parent, "Added dependency on '" + id + "'");

        // May need to invalidate parent and its dependents
        if (parent.state == State::done || parent.state == State::in_progress) {
            cascade_invalidate(parent_id,
                "New dependency '" + id + "' added");
        }
    }

    auto_save_if_enabled();

    std::string msg = "Created node '" + id + "'";
    if (!parent_id.empty()) msg += " as dependency of '" + parent_id + "'";
    return msg;
}

std::expected<std::string, std::string>
DependencyGraph::add_dependency(const std::string& node_id,
                                const std::string& depends_on,
                                const std::string& rationale) {
    if (!nodes_.count(node_id))
        return std::unexpected("Node '" + node_id + "' not found.");
    if (!nodes_.count(depends_on))
        return std::unexpected("Node '" + depends_on + "' not found.");

    // Check for duplicate
    auto& node = nodes_[node_id];
    for (const auto& dep : node.dependencies) {
        if (dep.node_id == depends_on)
            return std::unexpected("'" + node_id + "' already depends on '" + depends_on + "'.");
    }

    // Cycle detection: if node_id is reachable from depends_on, adding
    // this edge would create a cycle
    if (is_reachable(depends_on, node_id)) {
        return std::unexpected("Adding this dependency would create a cycle.");
    }

    node.dependencies.push_back({depends_on, rationale});
    append_changelog(node, "Added dependency on '" + depends_on + "'");
    priorities_dirty_ = true;

    // Cascade invalidation
    if (node.state == State::done || node.state == State::in_progress) {
        cascade_invalidate(node_id,
            "New dependency '" + depends_on + "' added");
    }

    auto_save_if_enabled();
    return "'" + node_id + "' now depends on '" + depends_on + "'";
}

// ============================================================================
// WORKING THE GRAPH
// ============================================================================

std::expected<std::string, std::string> DependencyGraph::next() {
    recompute_priorities();

    // Find root nodes (nodes that no other node depends on)
    std::set<std::string> non_roots;
    for (const auto& [id, node] : nodes_) {
        for (const auto& dep : node.dependencies) {
            non_roots.insert(dep.node_id);
        }
    }

    // Collect roots sorted by effective_priority desc
    std::vector<std::string> roots;
    for (const auto& [id, node] : nodes_) {
        if (node.state != State::deleted && !non_roots.count(id)) {
            roots.push_back(id);
        }
    }
    std::sort(roots.begin(), roots.end(),
        [this](const std::string& a, const std::string& b) {
            return nodes_[a].effective_priority > nodes_[b].effective_priority;
        });

    // DFS from each root
    for (const auto& root : roots) {
        auto result = dfs_next(root);
        if (result) {
            auto& node = nodes_[*result];
            node.state = State::in_progress;
            append_changelog(node, "Started (via next)");
            auto_save_if_enabled();

            std::string msg = "Next task: '" + *result + "'\n";
            msg += "Task: " + node.task + "\n";
            msg += "Priority: " + std::to_string(node.priority);
            msg += " (effective: " + std::to_string(node.effective_priority) + ")";
            if (node.state == State::invalidated) {
                msg += "\nNote: This task was previously completed but has been invalidated.";
            }
            return msg;
        }
    }

    // Check if everything is done or if we're blocked
    bool any_pending = false;
    for (const auto& [id, node] : nodes_) {
        if (node.state == State::pending || node.state == State::invalidated
            || node.state == State::in_progress) {
            any_pending = true;
            break;
        }
    }

    if (!any_pending) {
        return "All tasks are done.";
    }
    return "No actionable tasks. Remaining tasks are blocked by unfinished dependencies.";
}

std::expected<std::string, std::string>
DependencyGraph::done(const std::string& id, const std::string& summary) {
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return std::unexpected("Node '" + id + "' not found.");

    auto& node = it->second;
    if (node.state != State::in_progress)
        return std::unexpected("Node '" + id + "' is not in_progress (state: "
                               + state_to_string(node.state) + ").");

    node.state = State::done;
    append_changelog(node, "Done: " + summary);
    auto_save_if_enabled();

    return "Marked '" + id + "' as done.";
}

std::expected<std::string, std::string>
DependencyGraph::delete_node(const std::string& id, const std::string& reason) {
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return std::unexpected("Node '" + id + "' not found.");

    auto& node = it->second;
    if (node.state == State::deleted)
        return std::unexpected("Node '" + id + "' is already deleted.");

    node.pre_delete_state = node.state;
    node.state = State::deleted;
    append_changelog(node, "Deleted: " + reason);
    priorities_dirty_ = true;
    auto_save_if_enabled();

    return "Deleted node '" + id + "'.";
}

std::expected<std::string, std::string>
DependencyGraph::undelete(const std::string& id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return std::unexpected("Node '" + id + "' not found.");

    auto& node = it->second;
    if (node.state != State::deleted)
        return std::unexpected("Node '" + id + "' is not deleted.");

    node.state = node.pre_delete_state;
    append_changelog(node, "Undeleted, restored to " + state_to_string(node.state));
    priorities_dirty_ = true;
    auto_save_if_enabled();

    return "Restored node '" + id + "' to " + state_to_string(node.state) + ".";
}

// ============================================================================
// INFORMATION
// ============================================================================

std::expected<std::string, std::string>
DependencyGraph::log(const std::string& id, const std::string& entry) {
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return std::unexpected("Node '" + id + "' not found.");

    append_changelog(it->second, entry);
    auto_save_if_enabled();
    return "Logged entry on '" + id + "'.";
}

std::string DependencyGraph::status() {
    recompute_priorities();

    std::map<State, std::vector<const Node*>> grouped;
    for (const auto& [id, node] : nodes_) {
        if (node.state != State::deleted) {
            grouped[node.state].push_back(&node);
        }
    }

    if (grouped.empty()) return "No nodes in graph.";

    std::ostringstream ss;

    auto print_group = [&](State state, const std::string& header) {
        auto it = grouped.find(state);
        if (it == grouped.end()) return;

        ss << header << " (" << it->second.size() << "):\n";
        for (const auto* node : it->second) {
            int deps_met = 0;
            int deps_total = 0;
            for (const auto& dep : node->dependencies) {
                auto dit = nodes_.find(dep.node_id);
                if (dit == nodes_.end()) continue;
                deps_total++;
                if (dit->second.state == State::done
                    || dit->second.state == State::deleted)
                    deps_met++;
            }
            ss << "  - " << node->id << ": " << node->task;
            ss << " [p:" << node->priority
               << " ep:" << node->effective_priority << "]";
            if (deps_total > 0)
                ss << " (deps: " << deps_met << "/" << deps_total << ")";
            ss << "\n";
        }
    };

    print_group(State::in_progress, "In Progress");
    print_group(State::invalidated, "Invalidated");
    print_group(State::pending,     "Pending");
    print_group(State::done,        "Done");

    return ss.str();
}

std::string DependencyGraph::show(const std::string& id,
                                  const std::string& format) {
    recompute_priorities();

    std::map<std::string, const Node*> subdag;

    if (id.empty()) {
        for (const auto& [nid, node] : nodes_) {
            subdag[nid] = &node;
        }
    } else {
        if (!nodes_.count(id)) return "Node '" + id + "' not found.";
        collect_subdag(id, subdag);
    }

    if (subdag.empty()) return "Empty graph.";

    if (format == "json") return format_json(subdag);
    return format_text(subdag);
}

void DependencyGraph::collect_subdag(
        const std::string& id,
        std::map<std::string, const Node*>& visited) const {
    if (visited.count(id)) return;
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return;

    visited[id] = &it->second;
    for (const auto& dep : it->second.dependencies) {
        collect_subdag(dep.node_id, visited);
    }
}

std::string DependencyGraph::format_text(
        const std::map<std::string, const Node*>& nodes) const {
    // Find roots within this subdag (not depended on by anyone else in subdag)
    std::set<std::string> non_roots;
    for (const auto& [id, node] : nodes) {
        for (const auto& dep : node->dependencies) {
            if (nodes.count(dep.node_id)) {
                non_roots.insert(dep.node_id);
            }
        }
    }

    std::ostringstream ss;

    // Recursive printer
    std::set<std::string> printed;
    std::function<void(const std::string&, int)> print_node =
        [&](const std::string& nid, int depth) {
            auto it = nodes.find(nid);
            if (it == nodes.end()) return;

            const auto* node = it->second;
            std::string indent(depth * 2, ' ');

            ss << indent << state_indicator(node->state) << " "
               << node->id << ": " << node->task
               << " [p:" << node->priority
               << " ep:" << node->effective_priority << "]\n";

            if (printed.count(nid)) {
                if (!node->dependencies.empty()) {
                    ss << indent << "  (deps shown above)\n";
                }
                return;
            }
            printed.insert(nid);

            for (const auto& dep : node->dependencies) {
                if (nodes.count(dep.node_id)) {
                    print_node(dep.node_id, depth + 1);
                }
            }
        };

    for (const auto& [id, node] : nodes) {
        if (!non_roots.count(id)) {
            print_node(id, 0);
        }
    }

    return ss.str();
}

std::string DependencyGraph::format_json(
        const std::map<std::string, const Node*>& nodes) const {
    json result = json::object();
    for (const auto& [id, node] : nodes) {
        json deps = json::array();
        for (const auto& dep : node->dependencies) {
            deps.push_back({{"node_id", dep.node_id}, {"rationale", dep.rationale}});
        }
        json log = json::array();
        for (const auto& entry : node->changelog) {
            log.push_back({{"timestamp", entry.timestamp}, {"entry", entry.entry}});
        }
        result[id] = {
            {"task", node->task},
            {"state", state_to_string(node->state)},
            {"priority", node->priority},
            {"effective_priority", node->effective_priority},
            {"dependencies", deps},
            {"changelog", log}
        };
    }
    return result.dump(2);
}

// ============================================================================
// PERSISTENCE
// ============================================================================

json DependencyGraph::to_json() const {
    json nodes = json::object();
    for (const auto& [id, node] : nodes_) {
        json deps = json::array();
        for (const auto& dep : node.dependencies) {
            deps.push_back({{"node_id", dep.node_id}, {"rationale", dep.rationale}});
        }
        json log = json::array();
        for (const auto& entry : node.changelog) {
            log.push_back({{"timestamp", entry.timestamp}, {"entry", entry.entry}});
        }
        nodes[id] = {
            {"task", node.task},
            {"state", state_to_string(node.state)},
            {"priority", node.priority},
            {"effective_priority", node.effective_priority},
            {"pre_delete_state", state_to_string(node.pre_delete_state)},
            {"dependencies", deps},
            {"changelog", log}
        };
    }
    return {{"version", 1}, {"nodes", nodes}};
}

void DependencyGraph::from_json(const json& j) {
    nodes_.clear();

    const auto& nodes = j.at("nodes");
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        Node node;
        node.id = it.key();
        node.task = it.value().at("task");
        node.state = string_to_state(it.value().at("state"));
        node.priority = it.value().value("priority", 0);
        node.effective_priority = it.value().value("effective_priority", 0);
        node.pre_delete_state = string_to_state(
            it.value().value("pre_delete_state", "pending"));

        for (const auto& dep : it.value().at("dependencies")) {
            node.dependencies.push_back({dep.at("node_id"), dep.at("rationale")});
        }
        for (const auto& entry : it.value().at("changelog")) {
            node.changelog.push_back({entry.at("timestamp"), entry.at("entry")});
        }

        nodes_[node.id] = std::move(node);
    }
    priorities_dirty_ = true;
}

std::expected<std::string, std::string>
DependencyGraph::save(const std::string& filepath, bool auto_save) {
    std::ofstream f(filepath);
    if (!f.is_open())
        return std::unexpected("Could not open '" + filepath + "' for writing.");

    f << to_json().dump(2) << "\n";
    f.close();

    if (auto_save) {
        auto_save_path_ = filepath;
        auto_save_enabled_ = true;
    } else {
        auto_save_enabled_ = false;
        auto_save_path_.clear();
    }

    std::string msg = "Saved graph to '" + filepath + "'";
    if (auto_save) msg += " (auto-save enabled)";
    return msg;
}

std::expected<std::string, std::string>
DependencyGraph::load(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open())
        return std::unexpected("Could not open '" + filepath + "' for reading.");

    json j;
    try {
        f >> j;
    } catch (const json::exception& e) {
        return std::unexpected(std::string("Failed to parse: ") + e.what());
    }

    int version = j.value("version", 0);
    if (version != 1)
        return std::unexpected("Unsupported graph version: " + std::to_string(version));

    try {
        from_json(j);
    } catch (const json::exception& e) {
        return std::unexpected(std::string("Failed to load graph: ") + e.what());
    }

    // Auto-save is NOT inherited from loaded file
    auto_save_enabled_ = false;
    auto_save_path_.clear();

    return "Loaded graph from '" + filepath + "' ("
           + std::to_string(nodes_.size()) + " nodes)";
}

void DependencyGraph::auto_save_if_enabled() {
    if (!auto_save_enabled_) return;

    std::ofstream f(auto_save_path_);
    if (f.is_open()) {
        f << to_json().dump(2) << "\n";
    }
}
