#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "route/errors.hpp"
#include "route/graph.hpp"

namespace route {

/// Parameters of the deterministic lattice generator.
///
/// The same parameters always produce the same graph, and the same CSV bytes,
/// within one build environment. See route/random.hpp for why the pseudo-random
/// stream is specified rather than delegated to the standard library.
struct GeneratorParams {
  /// Lattice columns; must be at least 1.
  std::uint32_t width = 0;
  /// Lattice rows; must be at least 1.
  std::uint32_t height = 0;
  /// Seeds the whole generation: coordinates first, then diagonal decisions.
  std::uint64_t seed = 0;
  /// Nominal distance between neighbouring lattice sites, in metres.
  double spacing = 100.0;
  /// Maximum displacement of a site from its lattice position, as a fraction
  /// of `spacing`. Must be in [0, 0.5): at 0.5 two neighbours could coincide,
  /// which would put zero-length arcs in the graph.
  double jitter = 0.35;
  /// Probability that each of a cell's two diagonals is emitted, in [0, 1].
  double diagonal_rate = 0.25;
};

/// Checks the parameters and returns a human-readable problem, or an empty
/// string when they are usable.
///
/// Exposed so the command line can report a bad parameter as a usage error
/// while generate() reports the same problem as a validation error, without
/// the two checks drifting apart.
[[nodiscard]] std::string validate_generator_params(const GeneratorParams& params);

/// Builds a connected, directed, road-like lattice.
///
/// Node `r * width + c` sits near lattice position `(c * spacing,
/// r * spacing)`, displaced by an independent jitter in each axis. External
/// identifiers are the internal identifiers plus one, so they run from 1 in
/// row-major order.
///
/// Every horizontal and vertical neighbour pair is joined by two arcs, one in
/// each direction, which is what makes the graph connected by construction.
/// Each cell's two diagonals are then offered independently and accepted with
/// probability `diagonal_rate`, again as two arcs when accepted. The draw is
/// made whether or not it is accepted, so the stream does not depend on the
/// rate.
///
/// **Every arc weight is exactly the Euclidean distance between its endpoints**,
/// so the metric contract holds with equality and Euclidean A* is always
/// admissible on a generated graph. generate() verifies this on the finished
/// graph rather than assuming it.
///
/// \throws GraphValidationError when the parameters are unusable.
[[nodiscard]] Graph generate(const GeneratorParams& params);

/// Writes a graph in the project's CSV format.
///
/// Numbers are written with std::to_chars, which is locale-independent and
/// produces the shortest representation that reads back as the identical
/// double. A file written here therefore reloads bit-for-bit, so a generated
/// graph still satisfies the metric contract after a round trip through disk.
/// (That guarantee is within one build: it assumes std::hypot returns the same
/// value when the file is read as it did when it was written.)
///
/// On a formatting failure the stream's failbit is set; the caller is expected
/// to check the stream.
void write_csv(const Graph& graph, std::ostream& nodes, std::ostream& edges);

}  // namespace route
