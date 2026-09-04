#pragma once

#include <cstddef>
#include <string>

#include "route/graph.hpp"
#include "route/search.hpp"
#include "route/types.hpp"

namespace route_test {

struct PathCheck {
  bool valid = false;
  /// Cost re-summed from the graph's own arcs, independently of the search.
  route::Weight cost = 0.0;
  std::string reason;
};

/// Verifies that a found route really is a walk in the graph from `source` to
/// `target`, and re-sums its cost from the arc array.
///
/// For each consecutive pair the cheapest matching arc is used. Arcs are stored
/// in canonical (target, weight) order, so the first arc with the right target
/// is the cheapest one.
[[nodiscard]] inline PathCheck check_path(const route::Graph& graph,
                                          const route::SearchResult& result, route::NodeId source,
                                          route::NodeId target) {
  PathCheck check;
  if (!result.found()) {
    check.reason = "result reports no route";
    return check;
  }
  if (result.path.empty()) {
    check.reason = "a found route has an empty path";
    return check;
  }
  if (result.path.front() != source) {
    check.reason = "path does not start at the source";
    return check;
  }
  if (result.path.back() != target) {
    check.reason = "path does not end at the target";
    return check;
  }

  for (std::size_t i = 0; i + 1 < result.path.size(); ++i) {
    const route::NodeId from = result.path[i];
    const route::NodeId to = result.path[i + 1];
    bool linked = false;
    for (const route::Arc& arc : graph.arcs_from(from)) {
      if (arc.target == to) {
        check.cost += arc.weight;
        linked = true;
        break;
      }
    }
    if (!linked) {
      check.reason = "no arc from node " + std::to_string(route::index_of(from)) + " to node " +
                     std::to_string(route::index_of(to));
      return check;
    }
  }

  check.valid = true;
  return check;
}

}  // namespace route_test
