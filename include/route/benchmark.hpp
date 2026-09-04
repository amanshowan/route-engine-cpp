#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "route/errors.hpp"
#include "route/graph.hpp"
#include "route/search.hpp"
#include "route/types.hpp"

namespace route {

struct QueryPair {
  NodeId source{};
  NodeId target{};
};

/// The algorithms a benchmark compares, in their canonical reporting order.
inline constexpr std::array<Algorithm, 3> kBenchmarkAlgorithms = {
    Algorithm::Dijkstra, Algorithm::AStarZero, Algorithm::AStarEuclidean};

/// Builds a reproducible query set from a seed.
///
/// Draws are made with SplitMix64 (see route/random.hpp), two per query: the
/// source, then an offset in [1, node_count) that is added modulo node_count to
/// give the target. That construction makes source and target always distinct
/// without a rejection loop, so the number of draws per query is fixed and the
/// stream position depends only on the query index.
///
/// \throws GraphValidationError when the graph has fewer than two nodes, or
///         when `count` is zero.
[[nodiscard]] std::vector<QueryPair> make_query_pairs(const Graph& graph, std::uint64_t seed,
                                                      std::size_t count);

/// Totals for one algorithm over one full pass of the query set.
///
/// The counters are exact and identical in every repetition, so they are
/// recorded once; only the elapsed time varies, and it is kept per repetition.
struct AlgorithmSummary {
  Algorithm algorithm = Algorithm::Dijkstra;
  std::uint64_t nodes_expanded = 0;
  std::uint64_t arcs_examined = 0;
  std::uint64_t relaxations = 0;
  std::uint64_t pq_pushes = 0;
  std::uint64_t pq_pops = 0;
  std::uint64_t stale_pops = 0;
  /// Largest queue observed on any single query, not a sum.
  std::uint64_t max_queue_size = 0;
  /// Wall-clock seconds for one full pass over the query set, one entry per
  /// measured repetition. A secondary observation, not a portable result.
  std::vector<double> repetition_seconds;

  [[nodiscard]] double median_seconds() const;
};

struct BenchmarkReport {
  std::size_t queries = 0;
  std::size_t repetitions = 0;
  /// Queries for which a route exists. All algorithms agree on this, or the
  /// run fails.
  std::size_t routes_found = 0;
  std::array<AlgorithmSummary, kBenchmarkAlgorithms.size()> algorithms{};
};

/// Runs every algorithm over exactly the same graph and the same query set.
///
/// Fairness rules, all enforced here rather than left to the caller:
///   * one warm-up pass per algorithm, excluded from every reported number;
///   * the warm-up doubles as the agreement pass, so the comparison work is
///     never inside a timed region;
///   * every algorithm sees the identical query set in the identical order;
///   * the execution order of the algorithms rotates by one position on each
///     repetition, so none of them always runs first;
///   * counters from every repetition must match the first repetition's.
///
/// \throws HeuristicContractError when the graph violates the metric contract,
///         since Euclidean A* could not then be part of a fair comparison.
/// \throws GraphValidationError when the query set is empty or `repetitions`
///         is zero.
/// \throws BenchmarkAgreementError when the algorithms disagree about whether
///         a route exists or about its cost, or when a repetition's counters
///         differ from the first repetition's. No report is produced.
[[nodiscard]] BenchmarkReport run_benchmark(const Graph& graph,
                                            const std::vector<QueryPair>& queries,
                                            std::size_t repetitions);

}  // namespace route
