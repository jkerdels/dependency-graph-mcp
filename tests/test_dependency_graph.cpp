#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <dependency_graph.h>
#include <filesystem>
#include <fstream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool ok(const std::expected<std::string, std::string>& r) { return r.has_value(); }
static bool err(const std::expected<std::string, std::string>& r) { return !r.has_value(); }

TEST_SUITE("DependencyGraph") {

// ---------------------------------------------------------------------------
// Node creation
// ---------------------------------------------------------------------------

TEST_CASE("create_node success") {
    DependencyGraph g;
    auto r = g.create_node("A", "do A", 5, "some context");
    REQUIRE(ok(r));
    CHECK(r->find("A") != std::string::npos);
}

TEST_CASE("create_node duplicate returns error") {
    DependencyGraph g;
    g.create_node("A", "do A");
    auto r = g.create_node("A", "do A again");
    REQUIRE(err(r));
    CHECK(r.error().find("A") != std::string::npos);
}

TEST_CASE("create_nodes batch creates multiple nodes") {
    DependencyGraph g;
    json nodes = json::array({
        {{"id","X"},{"task","task X"},{"priority",1},{"context",""}},
        {{"id","Y"},{"task","task Y"},{"priority",2},{"context",""}}
    });
    auto r = g.create_nodes(nodes);
    REQUIRE(ok(r));
    // Both should now appear as actionable
    auto next = g.next();
    REQUIRE(ok(next));
}

// ---------------------------------------------------------------------------
// Dependencies
// ---------------------------------------------------------------------------

TEST_CASE("add_dependency success") {
    DependencyGraph g;
    g.create_node("A", "A");
    g.create_node("B", "B");
    auto r = g.add_dependency("B", "A", "B needs A");
    REQUIRE(ok(r));
}

TEST_CASE("add_dependency unknown node returns error") {
    DependencyGraph g;
    g.create_node("A", "A");
    auto r = g.add_dependency("MISSING", "A", "");
    REQUIRE(err(r));
}

TEST_CASE("add_dependency cycle detection") {
    DependencyGraph g;
    g.create_node("A", "A");
    g.create_node("B", "B");
    g.add_dependency("B", "A", "B->A");
    // A -> B would form a cycle
    auto r = g.add_dependency("A", "B", "would cycle");
    REQUIRE(err(r));
    CHECK(r.error().find("cycle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// next / start / done state machine
// ---------------------------------------------------------------------------

TEST_CASE("next on empty graph returns 'all done' message") {
    DependencyGraph g;
    auto r = g.next();
    // next() always succeeds; with no nodes everything is trivially "done"
    REQUIRE(ok(r));
    CHECK(r->find("done") != std::string::npos);
}

TEST_CASE("next returns actionable node and marks it in_progress") {
    DependencyGraph g;
    g.create_node("A", "A");
    auto r = g.next();
    REQUIRE(ok(r));
    CHECK(r->find("A") != std::string::npos);
    // A is now in_progress; no other actionable nodes — next() returns a
    // "no actionable" or "blocked" message (still a success value)
    auto r2 = g.next();
    REQUIRE(ok(r2));
    CHECK(r2->find("A") == std::string::npos);  // A not offered again
}

TEST_CASE("next respects dependency ordering") {
    DependencyGraph g;
    g.create_node("A", "A", 1);
    g.create_node("B", "B", 10);  // higher priority but depends on A
    g.add_dependency("B", "A", "");
    // A must come first because B is blocked
    auto r = g.next();
    REQUIRE(ok(r));
    CHECK(r->find("A") != std::string::npos);
}

TEST_CASE("next picks highest-priority independent node") {
    DependencyGraph g;
    g.create_node("lo",  "low",  1);
    g.create_node("hi",  "high", 9);
    g.create_node("mid", "mid",  5);
    auto r = g.next();
    REQUIRE(ok(r));
    CHECK(r->find("hi") != std::string::npos);
}

TEST_CASE("start transitions pending node to in_progress") {
    DependencyGraph g;
    g.create_node("A", "A");
    auto r = g.start("A");
    REQUIRE(ok(r));
}

TEST_CASE("start returns error for unknown node") {
    DependencyGraph g;
    auto r = g.start("NOPE");
    REQUIRE(err(r));
}

TEST_CASE("done transitions in_progress node to done") {
    DependencyGraph g;
    g.create_node("A", "A");
    g.start("A");
    auto r = g.done("A", "finished A");
    REQUIRE(ok(r));
}

TEST_CASE("done on pending node returns error") {
    DependencyGraph g;
    g.create_node("A", "A");
    // A is still pending, not in_progress
    auto r = g.done("A", "premature");
    REQUIRE(err(r));
}

TEST_CASE("done unlocks dependent node") {
    DependencyGraph g;
    g.create_node("A", "A");
    g.create_node("B", "B");
    g.add_dependency("B", "A", "");
    g.start("A");
    g.done("A", "done A");
    // B should now be actionable
    auto r = g.next();
    REQUIRE(ok(r));
    CHECK(r->find("B") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Cascade invalidation
// ---------------------------------------------------------------------------

TEST_CASE("reopening a done node cascades invalidation to dependents") {
    DependencyGraph g;
    g.create_node("A", "A");
    g.create_node("B", "B");
    g.add_dependency("B", "A", "");
    g.start("A"); g.done("A", "done");
    g.start("B"); g.done("B", "done");
    // Reopen A via start() — B should cascade-invalidate
    auto r = g.start("A");
    REQUIRE(ok(r));
    // A is in_progress; B is invalidated (A not done yet → B blocked).
    // Complete A again so B becomes actionable again.
    g.done("A", "done again");
    auto n = g.next();
    REQUIRE(ok(n));
    CHECK(n->find("B") != std::string::npos);
}

// ---------------------------------------------------------------------------
// done_batch
// ---------------------------------------------------------------------------

TEST_CASE("done_batch marks multiple nodes done") {
    DependencyGraph g;
    g.create_node("A", "A");
    g.create_node("B", "B");
    g.start("A"); g.start("B");
    json items = json::array({
        {{"id","A"},{"summary","done A"}},
        {{"id","B"},{"summary","done B"}}
    });
    auto r = g.done_batch(items);
    REQUIRE(ok(r));
    // next() returns "All tasks are done." (success value, not unexpected)
    auto n = g.next();
    REQUIRE(ok(n));
    CHECK(n->find("done") != std::string::npos);
}

// ---------------------------------------------------------------------------
// delete / undelete
// ---------------------------------------------------------------------------

TEST_CASE("delete_node and undelete round-trip") {
    DependencyGraph g;
    g.create_node("A", "A");
    auto r = g.delete_node("A", "not needed");
    REQUIRE(ok(r));
    // A is deleted, treated as met — undelete it
    auto r2 = g.undelete("A");
    REQUIRE(ok(r2));
    // A is now pending again
    auto n = g.next();
    REQUIRE(ok(n));
    CHECK(n->find("A") != std::string::npos);
}

TEST_CASE("delete_node unknown returns error") {
    DependencyGraph g;
    auto r = g.delete_node("NOPE", "reason");
    REQUIRE(err(r));
}

// ---------------------------------------------------------------------------
// log
// ---------------------------------------------------------------------------

TEST_CASE("log appends entry without state change") {
    DependencyGraph g;
    g.create_node("A", "A");
    auto r = g.log("A", "some note");
    REQUIRE(ok(r));
    // A is still pending
    auto n = g.next();
    REQUIRE(ok(n));
    CHECK(n->find("A") != std::string::npos);
}

TEST_CASE("log on unknown node returns error") {
    DependencyGraph g;
    auto r = g.log("NOPE", "note");
    REQUIRE(err(r));
}

// ---------------------------------------------------------------------------
// next_batch
// ---------------------------------------------------------------------------

TEST_CASE("next_batch lists actionable nodes without changing state") {
    DependencyGraph g;
    g.create_node("A", "A", 1);
    g.create_node("B", "B", 2);
    std::string batch = g.next_batch(0);
    CHECK(batch.find("A") != std::string::npos);
    CHECK(batch.find("B") != std::string::npos);
    // State unchanged — next() should still work
    REQUIRE(ok(g.next()));
}

// ---------------------------------------------------------------------------
// save / load round-trip
// ---------------------------------------------------------------------------

TEST_CASE("save and load round-trip preserves node state") {
    namespace fs = std::filesystem;
    auto path = fs::temp_directory_path() / "dag_test_roundtrip.json";

    DependencyGraph g1;
    g1.create_node("A", "task A", 3, "ctx A");
    g1.create_node("B", "task B", 7);
    g1.add_dependency("B", "A", "B needs A");
    g1.start("A");
    g1.done("A", "finished A");

    auto rs = g1.save(path.string());
    REQUIRE(ok(rs));

    DependencyGraph g2;
    auto rl = g2.load(path.string());
    REQUIRE(ok(rl));

    // B should now be actionable in the loaded graph
    auto n = g2.next();
    REQUIRE(ok(n));
    CHECK(n->find("B") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("load non-existent file returns error") {
    DependencyGraph g;
    auto r = g.load("/tmp/dag_no_such_file_xyz.json");
    REQUIRE(err(r));
}

} // TEST_SUITE
