#include "route/generator.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <ostream>
#include <system_error>
#include <vector>

#include "route/builder.hpp"
#include "route/random.hpp"

namespace route {
namespace {

/// std::to_chars needs at most 24 characters for a double in shortest
/// round-trip form; 64 leaves a wide margin.
constexpr std::size_t kNumberBufferSize = 64;

void write_number(std::ostream& out, double value) {
  std::array<char, kNumberBufferSize> buffer{};
  const std::to_chars_result result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (result.ec != std::errc{}) {
    out.setstate(std::ios::failbit);
    return;
  }
  out.write(buffer.data(), result.ptr - buffer.data());
}

}  // namespace

std::string validate_generator_params(const GeneratorParams& params) {
  if (params.width == 0) {
    return "width must be at least 1";
  }
  if (params.height == 0) {
    return "height must be at least 1";
  }
  if (!std::isfinite(params.spacing) || params.spacing <= 0.0) {
    return "spacing must be a finite positive number";
  }
  if (!std::isfinite(params.jitter) || params.jitter < 0.0 || params.jitter >= 0.5) {
    return "jitter must be at least 0 and less than 0.5";
  }
  if (!std::isfinite(params.diagonal_rate) || params.diagonal_rate < 0.0 ||
      params.diagonal_rate > 1.0) {
    return "diagonal-rate must be between 0 and 1 inclusive";
  }

  const std::uint64_t node_count =
      static_cast<std::uint64_t>(params.width) * static_cast<std::uint64_t>(params.height);
  if (node_count > max_supported_node_count()) {
    return "width * height is " + std::to_string(node_count) +
           " nodes, which exceeds the maximum supported node count of " +
           std::to_string(max_supported_node_count());
  }
  return {};
}

Graph generate(const GeneratorParams& params) {
  const std::string problem = validate_generator_params(params);
  if (!problem.empty()) {
    throw GraphValidationError("route::generate: " + problem);
  }

  const std::uint32_t width = params.width;
  const std::uint32_t height = params.height;
  // Bounded by max_supported_node_count(), which validate_generator_params has
  // already checked, so this product fits in 32 bits.
  const std::uint32_t node_count = width * height;

  GraphBuilder builder(node_count);
  SplitMix64 rng(params.seed);

  // Pass 1: coordinates, row-major, x jitter drawn before y jitter.
  std::vector<Coord> coordinates(node_count);
  const double amplitude = params.jitter * params.spacing;
  for (std::uint32_t row = 0; row < height; ++row) {
    for (std::uint32_t col = 0; col < width; ++col) {
      const double offset_x = (rng.next_unit() - 0.5) * 2.0 * amplitude;
      const double offset_y = (rng.next_unit() - 0.5) * 2.0 * amplitude;
      const std::uint32_t index = row * width + col;
      coordinates[index] = Coord{static_cast<double>(col) * params.spacing + offset_x,
                                 static_cast<double>(row) * params.spacing + offset_y};
      builder.set_node(make_node_id(index), coordinates[index], static_cast<ExternalId>(index) + 1);
    }
  }

  // Both directions of a connection carry the same weight: negation is exact
  // in IEEE arithmetic and std::hypot is even in each argument, so the metric
  // check sees equality for the arc and for its reverse.
  const auto connect = [&](std::uint32_t a, std::uint32_t b) {
    const Coord& from = coordinates[a];
    const Coord& to = coordinates[b];
    const Weight weight = std::hypot(to.x - from.x, to.y - from.y);
    builder.add_arc(make_node_id(a), make_node_id(b), weight);
    builder.add_arc(make_node_id(b), make_node_id(a), weight);
  };

  // Pass 2: the backbone. Every lattice neighbour pair, both directions. This
  // is what makes the graph connected without any repair step.
  for (std::uint32_t row = 0; row < height; ++row) {
    for (std::uint32_t col = 0; col < width; ++col) {
      const std::uint32_t index = row * width + col;
      if (col + 1 < width) {
        connect(index, index + 1);
      }
      if (row + 1 < height) {
        connect(index, index + width);
      }
    }
  }

  // Pass 3: diagonals. Two draws per cell, made whether or not the diagonal is
  // accepted, so that changing diagonal_rate does not shift the stream.
  for (std::uint32_t row = 0; row + 1 < height; ++row) {
    for (std::uint32_t col = 0; col + 1 < width; ++col) {
      const std::uint32_t top_left = row * width + col;
      const std::uint32_t top_right = top_left + 1;
      const std::uint32_t bottom_left = top_left + width;
      const std::uint32_t bottom_right = bottom_left + 1;
      if (rng.next_unit() < params.diagonal_rate) {
        connect(top_left, bottom_right);
      }
      if (rng.next_unit() < params.diagonal_rate) {
        connect(top_right, bottom_left);
      }
    }
  }

  Graph graph = builder.build();

  // The generator's contract is that its output is always usable by Euclidean
  // A*. Check it rather than assume it.
  if (!graph.metric_report().passes) {
    throw GraphValidationError(
        "route::generate: internal error, the generated graph violates the metric contract");
  }
  return graph;
}

void write_csv(const Graph& graph, std::ostream& nodes, std::ostream& edges) {
  nodes << "id,x,y\n";
  edges << "source,target,weight\n";

  for (std::uint32_t index = 0; index < graph.node_count(); ++index) {
    const NodeId node = make_node_id(index);
    const Coord coordinate = graph.coordinate(node);
    nodes << graph.external_id(node) << ',';
    write_number(nodes, coordinate.x);
    nodes << ',';
    write_number(nodes, coordinate.y);
    nodes << '\n';

    for (const Arc& arc : graph.arcs_from(node)) {
      edges << graph.external_id(node) << ',' << graph.external_id(arc.target) << ',';
      write_number(edges, arc.weight);
      edges << '\n';
    }
  }
}

}  // namespace route
