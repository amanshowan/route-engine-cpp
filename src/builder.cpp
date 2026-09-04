#include "route/builder.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace route {
namespace {

std::string node_text(NodeId id) { return std::to_string(index_of(id)); }

}  // namespace

void validate_node_capacity(std::size_t node_count) {
  if (node_count > max_supported_node_count()) {
    throw GraphValidationError("route::GraphBuilder: node count " + std::to_string(node_count) +
                               " exceeds the maximum supported node count of " +
                               std::to_string(max_supported_node_count()));
  }
}

void validate_arc_capacity(std::size_t arc_count) {
  if (arc_count > max_arc_count()) {
    throw GraphValidationError("route::GraphBuilder: arc count " + std::to_string(arc_count) +
                               " exceeds the maximum of " + std::to_string(max_arc_count()) +
                               " representable by a 32-bit CSR offset");
  }
}

GraphBuilder::GraphBuilder(std::size_t node_count) {
  validate_node_capacity(node_count);
  coordinates_.assign(node_count, Coord{});
  external_ids_.assign(node_count, ExternalId{0});
  assigned_.assign(node_count, std::uint8_t{0});
}

void GraphBuilder::require_not_consumed() const {
  if (consumed_) {
    throw GraphValidationError("route::GraphBuilder: builder was already consumed by build()");
  }
}

void GraphBuilder::set_node(NodeId id, Coord coordinate, ExternalId external_id) {
  require_not_consumed();

  const std::size_t index = index_of(id);
  if (index >= coordinates_.size()) {
    throw GraphValidationError("route::GraphBuilder: node id " + node_text(id) +
                               " is out of range for a builder with " +
                               std::to_string(coordinates_.size()) + " nodes");
  }
  if (assigned_[index] != 0) {
    throw GraphValidationError("route::GraphBuilder: node id " + node_text(id) +
                               " was already set");
  }
  if (!std::isfinite(coordinate.x) || !std::isfinite(coordinate.y)) {
    throw GraphValidationError("route::GraphBuilder: node id " + node_text(id) +
                               " has a non-finite coordinate");
  }

  coordinates_[index] = coordinate;
  external_ids_[index] = external_id;
  assigned_[index] = 1;
  ++assigned_count_;
}

void GraphBuilder::add_arc(NodeId source, NodeId target, Weight weight) {
  require_not_consumed();

  const std::size_t nodes = coordinates_.size();
  if (static_cast<std::size_t>(index_of(source)) >= nodes) {
    throw GraphValidationError("route::GraphBuilder: arc source " + node_text(source) +
                               " is out of range for a builder with " + std::to_string(nodes) +
                               " nodes");
  }
  if (static_cast<std::size_t>(index_of(target)) >= nodes) {
    throw GraphValidationError("route::GraphBuilder: arc target " + node_text(target) +
                               " is out of range for a builder with " + std::to_string(nodes) +
                               " nodes");
  }
  // Checked before the sign test so that a NaN weight, which compares false
  // against everything, cannot slip through as non-negative.
  if (!std::isfinite(weight)) {
    throw GraphValidationError("route::GraphBuilder: arc " + node_text(source) + " -> " +
                               node_text(target) + " has a non-finite weight");
  }
  if (weight < 0.0) {
    throw GraphValidationError("route::GraphBuilder: arc " + node_text(source) + " -> " +
                               node_text(target) + " has a negative weight");
  }

  validate_arc_capacity(staged_.size() + 1);
  staged_.push_back(StagedArc{source, target, weight});
}

Graph GraphBuilder::build() {
  require_not_consumed();

  if (assigned_count_ != coordinates_.size()) {
    std::size_t first_unset = 0;
    while (first_unset < assigned_.size() && assigned_[first_unset] != 0) {
      ++first_unset;
    }
    throw GraphValidationError("route::GraphBuilder: node id " + std::to_string(first_unset) +
                               " was never set (" +
                               std::to_string(coordinates_.size() - assigned_count_) + " of " +
                               std::to_string(coordinates_.size()) + " nodes unset)");
  }
  validate_arc_capacity(staged_.size());

  // Canonical order: (source, target, weight). Weights are known finite here,
  // so this comparator is a strict weak ordering. Arcs that compare equal on
  // all three keys are indistinguishable, so the sorted sequence is unique and
  // does not depend on the order in which arcs were added.
  std::sort(staged_.begin(), staged_.end(), [](const StagedArc& lhs, const StagedArc& rhs) {
    if (lhs.source != rhs.source) {
      return lhs.source < rhs.source;
    }
    if (lhs.target != rhs.target) {
      return lhs.target < rhs.target;
    }
    return lhs.weight < rhs.weight;
  });

  // Row sizes, then an exclusive prefix sum, give the CSR offsets. Because the
  // staged arcs are already sorted by source, copying them in order places each
  // row exactly where its offset says it is.
  const std::size_t nodes = coordinates_.size();
  std::vector<std::uint32_t> offsets(nodes + 1, 0);
  for (const StagedArc& arc : staged_) {
    ++offsets[static_cast<std::size_t>(index_of(arc.source)) + 1];
  }
  for (std::size_t row = 0; row < nodes; ++row) {
    offsets[row + 1] += offsets[row];
  }

  std::vector<Arc> arcs;
  arcs.reserve(staged_.size());
  for (const StagedArc& staged : staged_) {
    arcs.push_back(Arc{staged.target, staged.weight});
  }

  consumed_ = true;
  Graph graph(std::move(coordinates_), std::move(external_ids_), std::move(offsets),
              std::move(arcs));
  staged_.clear();
  staged_.shrink_to_fit();
  assigned_.clear();
  assigned_.shrink_to_fit();
  return graph;
}

}  // namespace route
