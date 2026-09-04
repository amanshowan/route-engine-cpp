#pragma once

#include <cstdint>
#include <limits>
#include <span>

#include "route/types.hpp"

namespace route {

/// Outcome of checking the metric contract required by Euclidean A*.
///
/// The contract is, for every directed arc (u, v):
///
///     weight(u, v) >= std::hypot(x_v - x_u, y_v - y_u)
///
/// The comparison is made against the stored floating-point values with no
/// tolerance. A tolerance could accept a weight fractionally below the computed
/// straight-line distance, which would break the admissibility argument that
/// the contract exists to guarantee.
///
/// When the contract holds, h(x) = distance(x, target) never overestimates the
/// remaining cost (admissible) and additionally satisfies
/// h(u) - h(v) <= weight(u, v) (consistent).
struct MetricReport {
  /// True when every arc satisfies the contract. Vacuously true for a graph
  /// with no arcs, including the empty graph and any one-node graph.
  bool passes = true;

  /// Number of arcs that violate the contract.
  std::uint64_t violating_arcs = 0;

  /// Endpoints of the first violating arc in canonical arc order.
  /// Meaningful only when violating_arcs > 0.
  NodeId first_violation_source{};
  NodeId first_violation_target{};

  /// Smallest weight/distance ratio over arcs whose straight-line distance is
  /// positive -- that is, the arc closest to violating the contract. Remains
  /// infinity when the graph contains no such arc (no arcs at all, or only
  /// zero-distance arcs such as self-loops and coincident endpoints).
  double worst_ratio = std::numeric_limits<double>::infinity();
};

/// Computes the metric report for a graph in CSR form.
///
/// Preconditions: `offsets` has size `coordinates.size() + 1`, is
/// non-decreasing, and its last element equals `arcs.size()`; every arc target
/// is a valid index into `coordinates`. Graph upholds all of these.
///
/// Zero-distance arcs (self-loops, or endpoints at identical coordinates) pass
/// whenever their weight is non-negative, which a built Graph always
/// guarantees, and are excluded from `worst_ratio`.
[[nodiscard]] MetricReport compute_metric_report(std::span<const Coord> coordinates,
                                                 std::span<const std::uint32_t> offsets,
                                                 std::span<const Arc> arcs);

}  // namespace route
