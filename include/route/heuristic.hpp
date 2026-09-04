#pragma once

#include <cmath>

#include "route/graph.hpp"
#include "route/types.hpp"

namespace route {

/// The heuristic that turns A* back into Dijkstra.
///
/// Used as the control in comparisons: any difference between Dijkstra and
/// zero-heuristic A* is a difference in machinery, not in guidance.
struct ZeroHeuristic {
  [[nodiscard]] Weight estimate(NodeId) const noexcept { return 0.0; }
};

/// Straight-line distance to a fixed target.
///
/// Admissible and consistent exactly when the graph satisfies the metric
/// contract (see route/metric.hpp), which Graph records at construction. The
/// search entry points are responsible for refusing this heuristic on a graph
/// whose stored report says the contract is violated.
class EuclideanHeuristic {
 public:
  EuclideanHeuristic(const Graph& graph, NodeId target)
      : graph_(&graph), target_(graph.coordinate(target)) {}

  [[nodiscard]] Weight estimate(NodeId from) const {
    const Coord position = graph_->coordinate(from);
    return std::hypot(target_.x - position.x, target_.y - position.y);
  }

 private:
  const Graph* graph_;
  Coord target_;
};

}  // namespace route
