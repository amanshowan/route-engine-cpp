#include "route/search.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

#include "route/heuristic.hpp"

namespace route {
namespace {

/// Parent sentinel. Node indices never reach UINT32_MAX because
/// max_supported_node_count() caps the node count at that value, which leaves
/// the highest identifier free to mean "no parent".
constexpr std::uint32_t kNoParent = std::numeric_limits<std::uint32_t>::max();

struct Entry {
  Weight priority = 0.0;
  NodeId node{};
};

/// Min-heap order on (priority, NodeId).
///
/// std::priority_queue is a max-heap, so this is the reversed comparison. The
/// node identifier breaks ties, which makes the pop order a total order and so
/// makes the whole search deterministic: equal-cost alternatives are always
/// resolved the same way. Because a node is pushed only on a strict
/// improvement, the (priority, node) pairs held in the queue are distinct.
struct HigherPriorityFirst {
  [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept {
    if (lhs.priority != rhs.priority) {
      return lhs.priority > rhs.priority;
    }
    return index_of(lhs.node) > index_of(rhs.node);
  }
};

void require_valid(const Graph& graph, NodeId id, const char* role) {
  if (!graph.contains(id)) {
    throw std::out_of_range("route::search: " + std::string(role) + " node id " +
                            std::to_string(index_of(id)) + " is out of range for a graph with " +
                            std::to_string(graph.node_count()) + " nodes");
  }
}

/// The one search implementation. Dijkstra is this with ZeroHeuristic, so the
/// two are the same code rather than two implementations kept in step by hand.
///
/// Lazy deletion: an improved node is pushed again and the superseded entry is
/// left in the queue, then discarded when it surfaces. This avoids an indexed
/// heap with decrease-key at the cost of extra entries, and that cost is
/// visible in stale_pops and max_queue_size rather than hidden.
template <typename Heuristic>
SearchResult search(const Graph& graph, NodeId source, NodeId target, const Heuristic& heuristic) {
  SearchResult result;

  const std::size_t node_count = graph.node_count();
  std::vector<Weight> distance(node_count, std::numeric_limits<Weight>::infinity());
  std::vector<std::uint32_t> parent(node_count, kNoParent);
  std::vector<std::uint8_t> settled(node_count, 0);

  std::priority_queue<Entry, std::vector<Entry>, HigherPriorityFirst> queue;

  distance[index_of(source)] = 0.0;
  queue.push(Entry{heuristic.estimate(source), source});
  ++result.stats.pq_pushes;
  result.stats.max_queue_size = std::max<std::uint64_t>(result.stats.max_queue_size, queue.size());

  bool reached = false;
  while (!queue.empty()) {
    const Entry entry = queue.top();
    queue.pop();
    ++result.stats.pq_pops;

    const std::size_t current = index_of(entry.node);
    if (settled[current] != 0) {
      ++result.stats.stale_pops;
      continue;
    }
    settled[current] = 1;
    ++result.stats.nodes_expanded;

    if (entry.node == target) {
      reached = true;
      break;
    }

    const Weight current_distance = distance[current];
    for (const Arc& arc : graph.arcs_from(entry.node)) {
      ++result.stats.arcs_examined;
      const std::size_t next = index_of(arc.target);
      if (settled[next] != 0) {
        continue;
      }
      const Weight candidate = current_distance + arc.weight;
      if (candidate < distance[next]) {
        distance[next] = candidate;
        parent[next] = static_cast<std::uint32_t>(current);
        ++result.stats.relaxations;
        queue.push(Entry{candidate + heuristic.estimate(arc.target), arc.target});
        ++result.stats.pq_pushes;
        result.stats.max_queue_size =
            std::max<std::uint64_t>(result.stats.max_queue_size, queue.size());
      }
    }
  }

  if (!reached) {
    return result;
  }

  result.status = SearchStatus::Found;
  result.cost = distance[index_of(target)];

  // Walk parents back from the target, then reverse. The source is the only
  // node on the path with no parent, so a source-equals-target query yields a
  // single-node path of cost zero.
  for (std::uint32_t at = index_of(target);; at = parent[at]) {
    result.path.push_back(make_node_id(at));
    if (at == index_of(source)) {
      break;
    }
  }
  std::reverse(result.path.begin(), result.path.end());
  return result;
}

}  // namespace

SearchResult dijkstra(const Graph& graph, NodeId source, NodeId target) {
  require_valid(graph, source, "source");
  require_valid(graph, target, "target");
  return search(graph, source, target, ZeroHeuristic{});
}

SearchResult astar_zero(const Graph& graph, NodeId source, NodeId target) {
  require_valid(graph, source, "source");
  require_valid(graph, target, "target");
  return search(graph, source, target, ZeroHeuristic{});
}

SearchResult astar_euclidean(const Graph& graph, NodeId source, NodeId target) {
  require_valid(graph, source, "source");
  require_valid(graph, target, "target");

  const MetricReport& report = graph.metric_report();
  if (!report.passes) {
    throw HeuristicContractError(
        "route::astar_euclidean: the graph violates the metric contract on " +
        std::to_string(report.violating_arcs) +
        " arc(s), so the straight-line heuristic is not admissible and the result could not be "
        "shown to be optimal; use dijkstra or astar-zero instead");
  }
  return search(graph, source, target, EuclideanHeuristic(graph, target));
}

std::string_view algorithm_name(Algorithm algorithm) noexcept {
  switch (algorithm) {
    case Algorithm::Dijkstra:
      return "dijkstra";
    case Algorithm::AStarZero:
      return "astar-zero";
    case Algorithm::AStarEuclidean:
      return "astar-euclidean";
  }
  return "unknown";
}

std::optional<Algorithm> parse_algorithm(std::string_view name) noexcept {
  if (name == "dijkstra") {
    return Algorithm::Dijkstra;
  }
  if (name == "astar-zero") {
    return Algorithm::AStarZero;
  }
  if (name == "astar-euclidean") {
    return Algorithm::AStarEuclidean;
  }
  return std::nullopt;
}

SearchResult run_search(const Graph& graph, NodeId source, NodeId target, Algorithm algorithm) {
  switch (algorithm) {
    case Algorithm::Dijkstra:
      return dijkstra(graph, source, target);
    case Algorithm::AStarZero:
      return astar_zero(graph, source, target);
    case Algorithm::AStarEuclidean:
      return astar_euclidean(graph, source, target);
  }
  throw std::out_of_range("route::run_search: unknown algorithm");
}

}  // namespace route
