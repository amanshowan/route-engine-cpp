#include "route/metric.hpp"

#include <cmath>
#include <cstddef>

namespace route {

MetricReport compute_metric_report(std::span<const Coord> coordinates,
                                   std::span<const std::uint32_t> offsets,
                                   std::span<const Arc> arcs) {
  MetricReport report;
  if (offsets.size() < 2) {
    // No nodes, therefore no arcs: the contract holds vacuously.
    return report;
  }

  const std::size_t node_count = offsets.size() - 1;
  for (std::size_t source = 0; source < node_count; ++source) {
    const Coord from = coordinates[source];
    const std::uint32_t row_end = offsets[source + 1];
    for (std::uint32_t k = offsets[source]; k < row_end; ++k) {
      const Arc& arc = arcs[k];
      const Coord to = coordinates[index_of(arc.target)];
      const double distance = std::hypot(to.x - from.x, to.y - from.y);

      // Written as the negation of the documented pass condition so that a
      // non-finite value, which comparison operators treat as unordered, is
      // reported as a violation instead of silently passing.
      if (!(arc.weight >= distance)) {
        ++report.violating_arcs;
        report.passes = false;
        if (report.violating_arcs == 1) {
          report.first_violation_source = make_node_id(static_cast<std::uint32_t>(source));
          report.first_violation_target = arc.target;
        }
      }

      if (distance > 0.0) {
        const double ratio = arc.weight / distance;
        if (ratio < report.worst_ratio) {
          report.worst_ratio = ratio;
        }
      }
    }
  }
  return report;
}

}  // namespace route
