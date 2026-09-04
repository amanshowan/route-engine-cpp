#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "route/errors.hpp"
#include "route/graph.hpp"
#include "route/types.hpp"

namespace route {

enum class SearchStatus {
  Found,
  Unreachable,
};

/// Diagnostic counters. Every field is exact and reproducible for a given
/// graph and query within one build; none of them is a timing measurement.
struct SearchStats {
  /// Entries inserted into the priority queue.
  std::uint64_t pq_pushes = 0;
  /// Entries removed from the priority queue, stale entries included.
  std::uint64_t pq_pops = 0;
  /// Pops of a node that had already been settled; the cost of lazy deletion.
  std::uint64_t stale_pops = 0;
  /// Pops that settled a node. The headline metric for comparing algorithms.
  std::uint64_t nodes_expanded = 0;
  /// Arcs scanned while expanding settled nodes.
  std::uint64_t arcs_examined = 0;
  /// Arc scans that lowered a tentative distance.
  std::uint64_t relaxations = 0;
  /// Largest priority-queue size observed, stale entries included.
  std::uint64_t max_queue_size = 0;
};

struct SearchResult {
  SearchStatus status = SearchStatus::Unreachable;
  /// Total cost of `path`. Infinity when no route was found.
  Weight cost = std::numeric_limits<Weight>::infinity();
  /// Source to target inclusive, in order. Empty when no route was found; a
  /// single element when source and target are the same node.
  std::vector<NodeId> path;
  /// Populated for both outcomes: the counters of an unreachable query
  /// describe the exhaustion of the source's reachable component.
  SearchStats stats;

  [[nodiscard]] bool found() const noexcept { return status == SearchStatus::Found; }
};

/// Shortest path with a uniform-cost search.
///
/// Preconditions: `source` and `target` are valid identifiers for `graph`.
/// Once they are, the search performs no data validation and reports no domain
/// errors. It is not noexcept: it allocates, so std::bad_alloc is possible.
///
/// \throws std::out_of_range when an identifier is not in [0, node_count()).
[[nodiscard]] SearchResult dijkstra(const Graph& graph, NodeId source, NodeId target);

/// A* with the zero heuristic. Observationally identical to dijkstra(),
/// counters included, because both instantiate the same search core.
///
/// \throws std::out_of_range when an identifier is not in [0, node_count()).
[[nodiscard]] SearchResult astar_zero(const Graph& graph, NodeId source, NodeId target);

/// A* with the straight-line-distance heuristic.
///
/// Refuses to run when the graph's stored metric report says the
/// weight >= straight-line-distance contract is violated, because the returned
/// route could not then be shown to be optimal. Dijkstra and zero-heuristic A*
/// remain available on such a graph.
///
/// \throws HeuristicContractError when the metric contract is violated.
/// \throws std::out_of_range when an identifier is not in [0, node_count()).
[[nodiscard]] SearchResult astar_euclidean(const Graph& graph, NodeId source, NodeId target);

enum class Algorithm {
  Dijkstra,
  AStarZero,
  AStarEuclidean,
};

/// The name accepted on the command line, e.g. "astar-euclidean".
[[nodiscard]] std::string_view algorithm_name(Algorithm algorithm) noexcept;

/// Parses a command-line algorithm name; empty when it is not one of them.
[[nodiscard]] std::optional<Algorithm> parse_algorithm(std::string_view name) noexcept;

/// Dispatches to the matching entry point above, with the same preconditions
/// and the same exceptions.
[[nodiscard]] SearchResult run_search(const Graph& graph, NodeId source, NodeId target,
                                      Algorithm algorithm);

}  // namespace route
