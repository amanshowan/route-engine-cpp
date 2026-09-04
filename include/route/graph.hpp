#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "route/metric.hpp"
#include "route/types.hpp"

namespace route {

class GraphBuilder;

/// Immutable, move-only directed graph in compressed sparse row (CSR) form.
///
/// A Graph can only be produced by GraphBuilder::build(), so every instance
/// satisfies the builder's invariants: every node has a finite coordinate and
/// an external identifier, every weight is finite and non-negative, and arcs
/// are stored in canonical (source, target, weight) order.
///
/// CSR storage keeps the whole arc array in one contiguous allocation. The
/// outgoing arcs of a node are a slice of that array, so a search expanding a
/// node walks memory linearly instead of chasing per-node allocations.
///
/// All accessors are read-only. Copy construction is deleted so that a graph is
/// never duplicated by accident on the way into a search.
class Graph {
 public:
  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;
  Graph(Graph&&) noexcept = default;
  Graph& operator=(Graph&&) noexcept = default;
  ~Graph() = default;

  [[nodiscard]] std::size_t node_count() const noexcept { return coordinates_.size(); }

  [[nodiscard]] std::size_t arc_count() const noexcept { return arcs_.size(); }

  /// True when `id` is a valid identifier for this graph.
  [[nodiscard]] bool contains(NodeId id) const noexcept {
    return static_cast<std::size_t>(index_of(id)) < coordinates_.size();
  }

  /// \throws std::out_of_range when `id` is not in [0, node_count()).
  [[nodiscard]] Coord coordinate(NodeId id) const;

  /// \throws std::out_of_range when `id` is not in [0, node_count()).
  [[nodiscard]] ExternalId external_id(NodeId id) const;

  /// Outgoing arcs of `id`, in canonical order, as a read-only view into the
  /// graph's arc array. The view is valid for the lifetime of the graph.
  ///
  /// \throws std::out_of_range when `id` is not in [0, node_count()).
  [[nodiscard]] std::span<const Arc> arcs_from(NodeId id) const;

  /// Metric contract report, computed once during construction so that later
  /// searches never repeat the O(arc_count) scan.
  [[nodiscard]] const MetricReport& metric_report() const noexcept { return metric_; }

 private:
  friend class GraphBuilder;

  Graph(std::vector<Coord> coordinates, std::vector<ExternalId> external_ids,
        std::vector<std::uint32_t> offsets, std::vector<Arc> arcs);

  std::vector<Coord> coordinates_;
  std::vector<ExternalId> external_ids_;
  /// CSR row boundaries; size is node_count() + 1 and the last entry is
  /// arc_count(). Arc indices are stored as 32-bit values, which is why the
  /// builder rejects arc counts that a 32-bit offset cannot represent.
  std::vector<std::uint32_t> offsets_;
  std::vector<Arc> arcs_;
  MetricReport metric_;
};

}  // namespace route
