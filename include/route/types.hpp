#pragma once

#include <cstdint>

namespace route {

/// Dense internal node identifier.
///
/// Values are contiguous in [0, node_count), which lets search algorithms index
/// flat arrays in O(1) with no hashing. The scoped enum prevents accidental
/// mixing with raw indices, arc counts and external identifiers.
enum class NodeId : std::uint32_t {};

/// Node identifier as it appears in input files and in CLI output.
///
/// External identifiers are arbitrary and are never used to index anything;
/// the mapping from ExternalId to NodeId belongs to the loader, not to the
/// graph-building abstraction.
using ExternalId = std::uint64_t;

/// Arc cost. Always finite and non-negative in a built Graph.
using Weight = double;

/// Projected planar coordinate, in metres.
///
/// This is deliberately not latitude/longitude: a planar metric makes the
/// straight-line distance used by the metric contract exact rather than an
/// approximation of a geodesic.
struct Coord {
  double x{};
  double y{};
};

/// One directed arc, stored in the CSR arc array.
///
/// Arc is naturally aligned and is not packed: the four bytes of padding
/// between `target` and `weight` are accepted rather than traded away. Packing
/// would place `weight` at an unaligned offset, so accessing it can cost extra
/// instructions on targets that permit unaligned loads and is not portable to
/// targets that do not; the four bytes per arc are not worth either.
struct Arc {
  NodeId target{};
  Weight weight{};
};

/// Underlying index of a node identifier.
[[nodiscard]] constexpr std::uint32_t index_of(NodeId id) noexcept {
  return static_cast<std::uint32_t>(id);
}

/// Node identifier for a dense index.
[[nodiscard]] constexpr NodeId make_node_id(std::uint32_t index) noexcept {
  return static_cast<NodeId>(index);
}

}  // namespace route
