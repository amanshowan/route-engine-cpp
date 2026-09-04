#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "route/csv.hpp"
#include "route/errors.hpp"
#include "route/graph.hpp"
#include "route/search.hpp"
#include "route/types.hpp"
#include "support/bellman_ford.hpp"
#include "support/path_checker.hpp"

namespace {

using route::Algorithm;
using route::ExternalId;
using route::Graph;
using route::NodeId;
using route::SearchResult;
using route::SearchStats;
using route::Weight;

constexpr Weight kInfinity = std::numeric_limits<Weight>::infinity();

Graph load(const std::string& nodes, const std::string& edges) {
  std::istringstream node_stream(nodes);
  std::istringstream edge_stream(edges);
  return route::load_graph(node_stream, "nodes.csv", edge_stream, "edges.csv");
}

// --- fixed small graphs with hand-checkable answers ------------------------

/// Two equal-cost routes from 1 to 4, so tie-breaking is observable.
Graph diamond() {
  return load(
      "id,x,y\n"
      "1,0,0\n"
      "2,1,1\n"
      "3,1,-1\n"
      "4,2,0\n",
      "source,target,weight\n"
      "1,2,2\n"
      "1,3,2\n"
      "2,4,2\n"
      "3,4,2\n");
}

/// A one-way chain plus an isolated node: reverse queries and node 4 are
/// unreachable. Every weight equals the straight-line distance exactly.
Graph one_way_chain() {
  return load(
      "id,x,y\n"
      "1,0,0\n"
      "2,10,0\n"
      "3,20,0\n"
      "4,100,0\n",
      "source,target,weight\n"
      "1,2,10\n"
      "2,3,10\n");
}

/// Node 2 is first reached at cost 5, then improved to 2, so the superseded
/// queue entry must be discarded when it surfaces.
Graph improves_after_pushing() {
  return load(
      "id,x,y\n"
      "1,0,0\n"
      "2,1,0\n"
      "3,0.5,0\n"
      "4,2,0\n",
      "source,target,weight\n"
      "1,2,5\n"
      "1,3,1\n"
      "3,2,1\n"
      "2,4,100\n");
}

/// A self-loop, parallel arcs and a zero-weight arc, all of which must be
/// harmless to a search.
Graph awkward_arcs() {
  return load(
      "id,x,y\n"
      "1,0,0\n"
      "2,3,4\n"
      "3,3,10\n",
      "source,target,weight\n"
      "1,1,0\n"
      "1,2,9\n"
      "1,2,5\n"
      "2,3,6\n"
      "3,2,6\n");
}

/// Arc weights far below the straight-line distance: Dijkstra is still
/// well-defined, but the Euclidean heuristic is not admissible.
Graph violates_metric_contract() {
  return load(
      "id,x,y\n"
      "1,0,0\n"
      "2,100,0\n",
      "source,target,weight\n"
      "1,2,1\n"
      "2,1,1\n");
}

struct NamedGraph {
  const char* name;
  Graph (*make)();
};

const NamedGraph kGraphs[] = {
    {"diamond", diamond},
    {"one_way_chain", one_way_chain},
    {"improves_after_pushing", improves_after_pushing},
    {"awkward_arcs", awkward_arcs},
    {"violates_metric_contract", violates_metric_contract},
};

// --- helpers ---------------------------------------------------------------

void expect_cost_agrees(Weight actual, Weight expected, const std::string& context) {
  if (std::isinf(expected)) {
    EXPECT_TRUE(std::isinf(actual)) << context;
    return;
  }
  EXPECT_NEAR(actual, expected, 1e-9 * std::max(1.0, std::abs(expected))) << context;
}

void expect_stats_equal(const SearchStats& lhs, const SearchStats& rhs,
                        const std::string& context) {
  EXPECT_EQ(lhs.pq_pushes, rhs.pq_pushes) << context;
  EXPECT_EQ(lhs.pq_pops, rhs.pq_pops) << context;
  EXPECT_EQ(lhs.stale_pops, rhs.stale_pops) << context;
  EXPECT_EQ(lhs.nodes_expanded, rhs.nodes_expanded) << context;
  EXPECT_EQ(lhs.arcs_examined, rhs.arcs_examined) << context;
  EXPECT_EQ(lhs.relaxations, rhs.relaxations) << context;
  EXPECT_EQ(lhs.max_queue_size, rhs.max_queue_size) << context;
}

std::vector<ExternalId> external_path(const Graph& graph, const SearchResult& result) {
  std::vector<ExternalId> ids;
  ids.reserve(result.path.size());
  for (const NodeId node : result.path) {
    ids.push_back(graph.external_id(node));
  }
  return ids;
}

// --- known answers ---------------------------------------------------------

TEST(Search, FindsTheShortestPathOnADiamond) {
  const Graph graph = diamond();
  const SearchResult result =
      route::dijkstra(graph, route::make_node_id(0), route::make_node_id(3));

  ASSERT_TRUE(result.found());
  EXPECT_DOUBLE_EQ(result.cost, 4.0);
  EXPECT_EQ(external_path(graph, result), (std::vector<ExternalId>{1, 2, 4}));
}

TEST(Search, TieBreakingIsDeterministicAndPrefersTheLowerNodeId) {
  const Graph graph = diamond();
  const NodeId source = route::make_node_id(0);
  const NodeId target = route::make_node_id(3);

  const SearchResult first = route::dijkstra(graph, source, target);
  for (int repeat = 0; repeat < 5; ++repeat) {
    const SearchResult again = route::dijkstra(graph, source, target);
    EXPECT_EQ(again.path, first.path) << "repeat " << repeat;
    EXPECT_DOUBLE_EQ(again.cost, first.cost) << "repeat " << repeat;
    expect_stats_equal(again.stats, first.stats, "repeat " + std::to_string(repeat));
  }

  // Both routes cost 4; the queue orders ties by node id, so the route through
  // node 2 (internal id 1) is taken rather than the one through node 3.
  EXPECT_EQ(external_path(graph, first), (std::vector<ExternalId>{1, 2, 4}));
}

TEST(Search, SourceEqualsTargetIsAZeroCostSingleNodeRoute) {
  for (const NamedGraph& entry : kGraphs) {
    const Graph graph = entry.make();
    for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
      const NodeId node = route::make_node_id(i);
      const SearchResult result = route::dijkstra(graph, node, node);
      ASSERT_TRUE(result.found()) << entry.name << " node " << i;
      EXPECT_DOUBLE_EQ(result.cost, 0.0) << entry.name << " node " << i;
      ASSERT_EQ(result.path.size(), std::size_t{1}) << entry.name << " node " << i;
      EXPECT_EQ(result.path.front(), node) << entry.name << " node " << i;
      EXPECT_EQ(result.stats.nodes_expanded, std::uint64_t{1}) << entry.name << " node " << i;
    }
  }
}

TEST(Search, UnreachableTargetGivesACleanNoRouteResult) {
  const Graph graph = one_way_chain();
  const SearchResult result =
      route::dijkstra(graph, route::make_node_id(0), route::make_node_id(3));

  EXPECT_FALSE(result.found());
  EXPECT_EQ(result.status, route::SearchStatus::Unreachable);
  EXPECT_EQ(result.cost, kInfinity);
  EXPECT_TRUE(result.path.empty());
  // The counters still describe the exhaustion of the reachable component.
  EXPECT_EQ(result.stats.nodes_expanded, std::uint64_t{3});
  EXPECT_GT(result.stats.pq_pops, std::uint64_t{0});
}

TEST(Search, DirectedArcsAreNotSymmetric) {
  const Graph graph = one_way_chain();
  const SearchResult forward =
      route::dijkstra(graph, route::make_node_id(0), route::make_node_id(2));
  ASSERT_TRUE(forward.found());
  EXPECT_DOUBLE_EQ(forward.cost, 20.0);

  const SearchResult backward =
      route::dijkstra(graph, route::make_node_id(2), route::make_node_id(0));
  EXPECT_FALSE(backward.found());
}

// --- cross-checks ----------------------------------------------------------

TEST(Search, EveryAlgorithmAgreesWithTheBellmanFordReferenceOnEveryPair) {
  for (const NamedGraph& entry : kGraphs) {
    const Graph graph = entry.make();
    const bool metric_ok = graph.metric_report().passes;

    for (std::uint32_t s = 0; s < graph.node_count(); ++s) {
      const NodeId source = route::make_node_id(s);
      const std::vector<Weight> reference = route_test::bellman_ford_distances(graph, source);

      for (std::uint32_t t = 0; t < graph.node_count(); ++t) {
        const NodeId target = route::make_node_id(t);
        const std::string context =
            std::string(entry.name) + " " + std::to_string(s) + " -> " + std::to_string(t);

        const SearchResult dijkstra = route::dijkstra(graph, source, target);
        expect_cost_agrees(dijkstra.cost, reference[t], context + " [dijkstra]");
        EXPECT_EQ(dijkstra.found(), !std::isinf(reference[t])) << context;

        const SearchResult zero = route::astar_zero(graph, source, target);
        expect_cost_agrees(zero.cost, reference[t], context + " [astar-zero]");

        if (metric_ok) {
          const SearchResult euclidean = route::astar_euclidean(graph, source, target);
          expect_cost_agrees(euclidean.cost, reference[t], context + " [astar-euclidean]");
          EXPECT_EQ(euclidean.found(), dijkstra.found()) << context;
          if (euclidean.found()) {
            const route_test::PathCheck check =
                route_test::check_path(graph, euclidean, source, target);
            EXPECT_TRUE(check.valid) << context << ": " << check.reason;
            expect_cost_agrees(check.cost, euclidean.cost, context + " [recomputed euclidean]");
          }
        }

        if (dijkstra.found()) {
          const route_test::PathCheck check =
              route_test::check_path(graph, dijkstra, source, target);
          EXPECT_TRUE(check.valid) << context << ": " << check.reason;
          expect_cost_agrees(check.cost, dijkstra.cost, context + " [recomputed dijkstra]");
        }
      }
    }
  }
}

TEST(Search, AStarWithTheZeroHeuristicIsObservationallyIdenticalToDijkstra) {
  for (const NamedGraph& entry : kGraphs) {
    const Graph graph = entry.make();
    for (std::uint32_t s = 0; s < graph.node_count(); ++s) {
      for (std::uint32_t t = 0; t < graph.node_count(); ++t) {
        const NodeId source = route::make_node_id(s);
        const NodeId target = route::make_node_id(t);
        const std::string context =
            std::string(entry.name) + " " + std::to_string(s) + " -> " + std::to_string(t);

        const SearchResult a = route::dijkstra(graph, source, target);
        const SearchResult b = route::astar_zero(graph, source, target);
        EXPECT_EQ(a.status, b.status) << context;
        EXPECT_EQ(a.path, b.path) << context;
        expect_cost_agrees(b.cost, a.cost, context);
        expect_stats_equal(b.stats, a.stats, context);
      }
    }
  }
}

TEST(Search, CounterInvariantsHold) {
  for (const NamedGraph& entry : kGraphs) {
    const Graph graph = entry.make();
    for (std::uint32_t s = 0; s < graph.node_count(); ++s) {
      for (std::uint32_t t = 0; t < graph.node_count(); ++t) {
        const SearchResult result =
            route::dijkstra(graph, route::make_node_id(s), route::make_node_id(t));
        const SearchStats& stats = result.stats;
        const std::string context =
            std::string(entry.name) + " " + std::to_string(s) + " -> " + std::to_string(t);

        EXPECT_EQ(stats.pq_pops, stats.nodes_expanded + stats.stale_pops) << context;
        EXPECT_GE(stats.pq_pushes, stats.pq_pops) << context;
        EXPECT_GE(stats.pq_pushes, stats.max_queue_size) << context;
        EXPECT_GE(stats.arcs_examined, stats.relaxations) << context;
        EXPECT_GE(stats.nodes_expanded, std::uint64_t{1}) << context;
      }
    }
  }
}

// --- lazy deletion ---------------------------------------------------------

TEST(Search, LazyDeletionDiscardsSupersededQueueEntries) {
  const Graph graph = improves_after_pushing();
  const SearchResult result =
      route::dijkstra(graph, route::make_node_id(0), route::make_node_id(3));

  ASSERT_TRUE(result.found());
  EXPECT_DOUBLE_EQ(result.cost, 102.0);
  EXPECT_EQ(external_path(graph, result), (std::vector<ExternalId>{1, 3, 2, 4}));
  EXPECT_GE(result.stats.stale_pops, std::uint64_t{1});
  EXPECT_EQ(result.stats.pq_pops, result.stats.nodes_expanded + result.stats.stale_pops);
}

// --- heuristic contract ----------------------------------------------------

TEST(Search, EuclideanAStarRefusesWhenTheMetricContractIsViolated) {
  const Graph graph = violates_metric_contract();
  ASSERT_FALSE(graph.metric_report().passes);

  EXPECT_THROW((void)route::astar_euclidean(graph, route::make_node_id(0), route::make_node_id(1)),
               route::HeuristicContractError);
  EXPECT_THROW((void)route::run_search(graph, route::make_node_id(0), route::make_node_id(1),
                                       Algorithm::AStarEuclidean),
               route::HeuristicContractError);
}

TEST(Search, DijkstraAndZeroHeuristicAStarStillRunOnAContractViolatingGraph) {
  const Graph graph = violates_metric_contract();
  ASSERT_FALSE(graph.metric_report().passes);

  const SearchResult dijkstra =
      route::dijkstra(graph, route::make_node_id(0), route::make_node_id(1));
  ASSERT_TRUE(dijkstra.found());
  EXPECT_DOUBLE_EQ(dijkstra.cost, 1.0);

  const SearchResult zero =
      route::astar_zero(graph, route::make_node_id(0), route::make_node_id(1));
  ASSERT_TRUE(zero.found());
  EXPECT_DOUBLE_EQ(zero.cost, 1.0);
}

TEST(Search, EuclideanAStarRunsWhenTheContractHolds) {
  const Graph graph = one_way_chain();
  ASSERT_TRUE(graph.metric_report().passes);

  const SearchResult result =
      route::astar_euclidean(graph, route::make_node_id(0), route::make_node_id(2));
  ASSERT_TRUE(result.found());
  EXPECT_DOUBLE_EQ(result.cost, 20.0);
  EXPECT_EQ(external_path(graph, result), (std::vector<ExternalId>{1, 2, 3}));
}

// --- preconditions and dispatch -------------------------------------------

TEST(Search, OutOfRangeIdentifiersThrow) {
  const Graph graph = diamond();
  const NodeId valid = route::make_node_id(0);
  const NodeId invalid = route::make_node_id(99);

  EXPECT_THROW((void)route::dijkstra(graph, invalid, valid), std::out_of_range);
  EXPECT_THROW((void)route::dijkstra(graph, valid, invalid), std::out_of_range);
  EXPECT_THROW((void)route::astar_zero(graph, invalid, valid), std::out_of_range);
  EXPECT_THROW((void)route::astar_euclidean(graph, valid, invalid), std::out_of_range);
}

TEST(Search, AlgorithmNamesRoundTripAndDispatch) {
  for (const Algorithm algorithm :
       {Algorithm::Dijkstra, Algorithm::AStarZero, Algorithm::AStarEuclidean}) {
    const auto parsed = route::parse_algorithm(route::algorithm_name(algorithm));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value(), algorithm);
  }
  EXPECT_FALSE(route::parse_algorithm("astar").has_value());
  EXPECT_FALSE(route::parse_algorithm("").has_value());
  EXPECT_FALSE(route::parse_algorithm("Dijkstra").has_value());

  const Graph graph = diamond();
  const SearchResult direct =
      route::astar_euclidean(graph, route::make_node_id(0), route::make_node_id(3));
  const SearchResult dispatched = route::run_search(
      graph, route::make_node_id(0), route::make_node_id(3), Algorithm::AStarEuclidean);
  EXPECT_EQ(direct.path, dispatched.path);
  EXPECT_DOUBLE_EQ(direct.cost, dispatched.cost);
  expect_stats_equal(direct.stats, dispatched.stats, "run_search dispatch");
}

}  // namespace
