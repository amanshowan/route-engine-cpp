#include "route/graph.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace route {
namespace {

[[noreturn]] void throw_out_of_range(NodeId id, std::size_t node_count) {
  throw std::out_of_range("route::Graph: node id " + std::to_string(index_of(id)) +
                          " is out of range for a graph with " + std::to_string(node_count) +
                          " nodes");
}

}  // namespace

Graph::Graph(std::vector<Coord> coordinates, std::vector<ExternalId> external_ids,
             std::vector<std::uint32_t> offsets, std::vector<Arc> arcs)
    : coordinates_(std::move(coordinates)),
      external_ids_(std::move(external_ids)),
      offsets_(std::move(offsets)),
      arcs_(std::move(arcs)),
      metric_(compute_metric_report(coordinates_, offsets_, arcs_)) {}

Coord Graph::coordinate(NodeId id) const {
  if (!contains(id)) {
    throw_out_of_range(id, node_count());
  }
  return coordinates_[index_of(id)];
}

ExternalId Graph::external_id(NodeId id) const {
  if (!contains(id)) {
    throw_out_of_range(id, node_count());
  }
  return external_ids_[index_of(id)];
}

std::span<const Arc> Graph::arcs_from(NodeId id) const {
  if (!contains(id)) {
    throw_out_of_range(id, node_count());
  }
  const std::size_t row = index_of(id);
  const std::uint32_t begin = offsets_[row];
  const std::uint32_t end = offsets_[row + 1];
  return std::span<const Arc>(arcs_.data() + begin, end - begin);
}

}  // namespace route
