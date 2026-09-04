#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "route/errors.hpp"
#include "route/graph.hpp"
#include "route/types.hpp"

namespace route {

/// Largest node count this engine supports.
///
/// This is a deliberately conservative project limit, not the capacity of the
/// identifier type: NodeId's 32-bit representation can hold every value from 0
/// through UINT32_MAX, so capping the count at UINT32_MAX leaves the highest
/// identifier unused. The cap is set here so that node counts and CSR offsets
/// share one bound and no index arithmetic can approach the type's edge.
[[nodiscard]] constexpr std::size_t max_supported_node_count() noexcept {
  return static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
}

/// Largest arc count representable by a 32-bit CSR offset.
[[nodiscard]] constexpr std::size_t max_arc_count() noexcept {
  return static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
}

/// \throws GraphValidationError when `node_count` exceeds
///         max_supported_node_count().
void validate_node_capacity(std::size_t node_count);

/// \throws GraphValidationError when `arc_count` cannot be represented.
void validate_arc_capacity(std::size_t arc_count);

/// Accumulates nodes and directed arcs, validates them, and produces a Graph.
///
/// The builder works exclusively in dense internal identifiers. Resolving
/// external identifiers, detecting duplicate external identifiers and reporting
/// unknown endpoints are loader concerns and are deliberately kept out of this
/// class.
///
/// Every method validates its arguments and throws GraphValidationError rather
/// than narrowing, truncating or indexing out of bounds.
class GraphBuilder {
 public:
  /// \throws GraphValidationError when `node_count` exceeds
  ///         max_supported_node_count().
  explicit GraphBuilder(std::size_t node_count);

  /// Assigns the coordinate and external identifier of a node. Each node must
  /// be set exactly once before build().
  ///
  /// \throws GraphValidationError when `id` is out of range, when the node has
  ///         already been set, or when the coordinate is not finite.
  void set_node(NodeId id, Coord coordinate, ExternalId external_id);

  /// Adds one directed arc. Zero weights, self-loops and parallel arcs are all
  /// accepted; a bidirectional connection is two calls.
  ///
  /// \throws GraphValidationError when an endpoint is out of range, when the
  ///         weight is not finite or is negative, or when the arc would exceed
  ///         max_arc_count().
  void add_arc(NodeId source, NodeId target, Weight weight);

  [[nodiscard]] std::size_t node_count() const noexcept { return coordinates_.size(); }

  [[nodiscard]] std::size_t assigned_node_count() const noexcept { return assigned_count_; }

  [[nodiscard]] std::size_t staged_arc_count() const noexcept { return staged_.size(); }

  /// Canonicalises the staged arcs, builds the CSR arrays and returns the
  /// immutable graph.
  ///
  /// The builder's buffers are moved into the graph, so the builder is consumed
  /// by this call: any later use throws rather than operating on moved-from
  /// state.
  ///
  /// \throws GraphValidationError when any node was never set, when the arc
  ///         count is not representable, or when the builder was already
  ///         consumed.
  [[nodiscard]] Graph build();

 private:
  struct StagedArc {
    NodeId source{};
    NodeId target{};
    Weight weight{};
  };

  void require_not_consumed() const;

  std::vector<Coord> coordinates_;
  std::vector<ExternalId> external_ids_;
  std::vector<std::uint8_t> assigned_;
  std::vector<StagedArc> staged_;
  std::size_t assigned_count_ = 0;
  bool consumed_ = false;
};

}  // namespace route
