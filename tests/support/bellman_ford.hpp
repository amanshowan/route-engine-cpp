#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "route/graph.hpp"
#include "route/types.hpp"

namespace route_test {

/// Deliberately naive O(V * E) shortest-distance reference.
///
/// It shares no code with the search core, which is the point: agreement
/// between this and Dijkstra is independent evidence, not the implementation
/// grading its own homework.
[[nodiscard]] inline std::vector<route::Weight> bellman_ford_distances(const route::Graph& graph,
                                                                       route::NodeId source) {
  constexpr route::Weight kInfinity = std::numeric_limits<route::Weight>::infinity();
  const std::size_t node_count = graph.node_count();
  std::vector<route::Weight> distance(node_count, kInfinity);
  if (node_count == 0) {
    return distance;
  }
  distance[route::index_of(source)] = 0.0;

  for (std::size_t round = 0; round + 1 < node_count; ++round) {
    bool changed = false;
    for (std::uint32_t u = 0; u < node_count; ++u) {
      const route::Weight from_distance = distance[u];
      if (from_distance == kInfinity) {
        continue;
      }
      for (const route::Arc& arc : graph.arcs_from(route::make_node_id(u))) {
        const route::Weight candidate = from_distance + arc.weight;
        if (candidate < distance[route::index_of(arc.target)]) {
          distance[route::index_of(arc.target)] = candidate;
          changed = true;
        }
      }
    }
    if (!changed) {
      break;
    }
  }
  return distance;
}

}  // namespace route_test
