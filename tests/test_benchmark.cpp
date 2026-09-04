#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "route/benchmark.hpp"
#include "route/csv.hpp"
#include "route/errors.hpp"
#include "route/generator.hpp"
#include "route/graph.hpp"
#include "route/search.hpp"
#include "route/types.hpp"

namespace {

using route::BenchmarkReport;
using route::GeneratorParams;
using route::Graph;
using route::GraphValidationError;
using route::QueryPair;

Graph lattice(std::uint32_t width, std::uint32_t height, std::uint64_t seed) {
  GeneratorParams params;
  params.width = width;
  params.height = height;
  params.seed = seed;
  return route::generate(params);
}

Graph from_csv(const std::string& nodes, const std::string& edges) {
  std::istringstream node_stream(nodes);
  std::istringstream edge_stream(edges);
  return route::load_graph(node_stream, "nodes.csv", edge_stream, "edges.csv");
}

// --- query generation ------------------------------------------------------

TEST(Benchmark, QuerySetsAreDeterministicForASeed) {
  const Graph graph = lattice(6, 5, 1);
  const std::vector<QueryPair> first = route::make_query_pairs(graph, 4321, 50);
  const std::vector<QueryPair> second = route::make_query_pairs(graph, 4321, 50);

  ASSERT_EQ(first.size(), second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].source, second[i].source) << "query " << i;
    EXPECT_EQ(first[i].target, second[i].target) << "query " << i;
  }
}

TEST(Benchmark, DifferentSeedsProduceDifferentQuerySets) {
  const Graph graph = lattice(6, 5, 1);
  const std::vector<QueryPair> a = route::make_query_pairs(graph, 1, 50);
  const std::vector<QueryPair> b = route::make_query_pairs(graph, 2, 50);

  bool any_difference = false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].source != b[i].source || a[i].target != b[i].target) {
      any_difference = true;
      break;
    }
  }
  EXPECT_TRUE(any_difference);
}

TEST(Benchmark, QueriesAreInRangeAndNeverDegenerate) {
  const Graph graph = lattice(5, 4, 9);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 77, 500);

  ASSERT_EQ(queries.size(), std::size_t{500});
  for (std::size_t i = 0; i < queries.size(); ++i) {
    EXPECT_TRUE(graph.contains(queries[i].source)) << "query " << i;
    EXPECT_TRUE(graph.contains(queries[i].target)) << "query " << i;
    EXPECT_NE(queries[i].source, queries[i].target) << "query " << i;
  }
}

TEST(Benchmark, QueriesAreDrawnFromAcrossTheGraph) {
  const Graph graph = lattice(5, 4, 9);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 77, 2000);

  std::set<std::uint32_t> sources;
  for (const QueryPair& query : queries) {
    sources.insert(route::index_of(query.source));
  }
  EXPECT_EQ(sources.size(), graph.node_count());
}

TEST(Benchmark, QueryGenerationRejectsGraphsThatAreTooSmall) {
  const Graph single = from_csv("id,x,y\n1,0,0\n", "source,target,weight\n");
  EXPECT_THROW((void)route::make_query_pairs(single, 1, 10), GraphValidationError);

  const Graph empty = from_csv("id,x,y\n", "source,target,weight\n");
  EXPECT_THROW((void)route::make_query_pairs(empty, 1, 10), GraphValidationError);

  const Graph pair = lattice(2, 1, 1);
  EXPECT_NO_THROW((void)route::make_query_pairs(pair, 1, 10));
}

TEST(Benchmark, QueryGenerationRejectsAZeroCount) {
  const Graph graph = lattice(4, 4, 1);
  EXPECT_THROW((void)route::make_query_pairs(graph, 1, 0), GraphValidationError);
}

// --- running the comparison ------------------------------------------------

TEST(Benchmark, ReportsEveryAlgorithmOverTheSameQuerySet) {
  const Graph graph = lattice(8, 6, 2024);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 5, 40);
  const BenchmarkReport report = route::run_benchmark(graph, queries, 3);

  EXPECT_EQ(report.queries, std::size_t{40});
  EXPECT_EQ(report.repetitions, std::size_t{3});
  // The lattice backbone is bidirectional, so every query is answerable.
  EXPECT_EQ(report.routes_found, std::size_t{40});

  ASSERT_EQ(report.algorithms.size(), route::kBenchmarkAlgorithms.size());
  for (std::size_t i = 0; i < report.algorithms.size(); ++i) {
    EXPECT_EQ(report.algorithms[i].algorithm, route::kBenchmarkAlgorithms[i]) << "slot " << i;
    EXPECT_GT(report.algorithms[i].nodes_expanded, std::uint64_t{0}) << "slot " << i;
    EXPECT_EQ(report.algorithms[i].repetition_seconds.size(), std::size_t{3}) << "slot " << i;
  }
}

TEST(Benchmark, DijkstraAndZeroHeuristicAStarProduceIdenticalCounters) {
  const Graph graph = lattice(7, 6, 314);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 8, 30);
  const BenchmarkReport report = route::run_benchmark(graph, queries, 2);

  const route::AlgorithmSummary& dijkstra = report.algorithms[0];
  const route::AlgorithmSummary& zero = report.algorithms[1];
  ASSERT_EQ(dijkstra.algorithm, route::Algorithm::Dijkstra);
  ASSERT_EQ(zero.algorithm, route::Algorithm::AStarZero);

  EXPECT_EQ(dijkstra.nodes_expanded, zero.nodes_expanded);
  EXPECT_EQ(dijkstra.arcs_examined, zero.arcs_examined);
  EXPECT_EQ(dijkstra.relaxations, zero.relaxations);
  EXPECT_EQ(dijkstra.pq_pushes, zero.pq_pushes);
  EXPECT_EQ(dijkstra.pq_pops, zero.pq_pops);
  EXPECT_EQ(dijkstra.stale_pops, zero.stale_pops);
  EXPECT_EQ(dijkstra.max_queue_size, zero.max_queue_size);
}

TEST(Benchmark, RoutesFoundMatchesDirectSearches) {
  const Graph graph = lattice(6, 5, 606);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 3, 25);
  const BenchmarkReport report = route::run_benchmark(graph, queries, 1);

  std::size_t found = 0;
  for (const QueryPair& query : queries) {
    if (route::dijkstra(graph, query.source, query.target).found()) {
      ++found;
    }
  }
  EXPECT_EQ(report.routes_found, found);
}

TEST(Benchmark, CountersDoNotVaryWithTheNumberOfRepetitions) {
  const Graph graph = lattice(6, 5, 55);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 4, 20);

  const BenchmarkReport once = route::run_benchmark(graph, queries, 1);
  const BenchmarkReport thrice = route::run_benchmark(graph, queries, 3);

  for (std::size_t i = 0; i < once.algorithms.size(); ++i) {
    EXPECT_EQ(once.algorithms[i].nodes_expanded, thrice.algorithms[i].nodes_expanded)
        << "slot " << i;
    EXPECT_EQ(once.algorithms[i].arcs_examined, thrice.algorithms[i].arcs_examined) << "slot " << i;
    EXPECT_EQ(once.algorithms[i].max_queue_size, thrice.algorithms[i].max_queue_size)
        << "slot " << i;
  }
}

TEST(Benchmark, ElapsedTimesAreRecordedPerRepetitionAndAreUsable) {
  // Deliberately not a timing comparison: only that the figures exist, are
  // finite and are non-negative. Nothing in the test suite asserts that one
  // algorithm is faster than another.
  const Graph graph = lattice(6, 5, 12);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 6, 15);
  const BenchmarkReport report = route::run_benchmark(graph, queries, 4);

  for (const route::AlgorithmSummary& summary : report.algorithms) {
    ASSERT_EQ(summary.repetition_seconds.size(), std::size_t{4});
    for (const double seconds : summary.repetition_seconds) {
      EXPECT_TRUE(std::isfinite(seconds));
      EXPECT_GE(seconds, 0.0);
    }
    EXPECT_TRUE(std::isfinite(summary.median_seconds()));
    EXPECT_GE(summary.median_seconds(), 0.0);
  }
}

TEST(Benchmark, MedianOfNoRepetitionsIsNotANumber) {
  route::AlgorithmSummary summary;
  EXPECT_TRUE(std::isnan(summary.median_seconds()));

  summary.repetition_seconds = {3.0, 1.0, 2.0};
  EXPECT_DOUBLE_EQ(summary.median_seconds(), 2.0);
  summary.repetition_seconds = {4.0, 1.0, 3.0, 2.0};
  EXPECT_DOUBLE_EQ(summary.median_seconds(), 2.5);
}

// --- refusals --------------------------------------------------------------

TEST(Benchmark, RefusesAGraphThatViolatesTheMetricContract) {
  const Graph graph = from_csv(
      "id,x,y\n"
      "1,0,0\n"
      "2,100,0\n",
      "source,target,weight\n"
      "1,2,1\n"
      "2,1,1\n");
  ASSERT_FALSE(graph.metric_report().passes);

  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 1, 4);
  EXPECT_THROW((void)route::run_benchmark(graph, queries, 1), route::HeuristicContractError);
}

TEST(Benchmark, RejectsAnEmptyQuerySetAndZeroRepetitions) {
  const Graph graph = lattice(4, 4, 1);
  const std::vector<QueryPair> queries = route::make_query_pairs(graph, 1, 5);

  EXPECT_THROW((void)route::run_benchmark(graph, {}, 1), GraphValidationError);
  EXPECT_THROW((void)route::run_benchmark(graph, queries, 0), GraphValidationError);
}

}  // namespace
