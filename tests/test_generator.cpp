#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "route/csv.hpp"
#include "route/errors.hpp"
#include "route/generator.hpp"
#include "route/graph.hpp"
#include "route/random.hpp"
#include "route/search.hpp"
#include "route/types.hpp"

namespace {

using route::Arc;
using route::Coord;
using route::ExternalId;
using route::GeneratorParams;
using route::Graph;
using route::GraphValidationError;
using route::SplitMix64;

GeneratorParams params(std::uint32_t width, std::uint32_t height, std::uint64_t seed) {
  GeneratorParams p;
  p.width = width;
  p.height = height;
  p.seed = seed;
  return p;
}

std::pair<std::string, std::string> to_csv(const Graph& graph) {
  std::ostringstream nodes;
  std::ostringstream edges;
  route::write_csv(graph, nodes, edges);
  return {nodes.str(), edges.str()};
}

/// Backbone arcs for a width x height lattice: two per neighbouring pair.
std::uint64_t backbone_arcs(std::uint64_t width, std::uint64_t height) {
  return 2 * (height * (width - 1) + width * (height - 1));
}

// --- the pseudo-random stream ---------------------------------------------

TEST(SplitMix, ProducesTheDocumentedReferenceStream) {
  // Reference values computed with an independent implementation of the
  // published SplitMix64 algorithm; 0xe220a8397b1dcdaf for seed 0 is the value
  // quoted in the reference sources.
  SplitMix64 zero(0);
  EXPECT_EQ(zero.next(), 0xe220a8397b1dcdafULL);
  EXPECT_EQ(zero.next(), 0x6e789e6aa1b965f4ULL);
  EXPECT_EQ(zero.next(), 0x06c45d188009454fULL);

  SplitMix64 one(1);
  EXPECT_EQ(one.next(), 0x910a2dec89025cc1ULL);
  EXPECT_EQ(one.next(), 0xbeeb8da1658eec67ULL);
  EXPECT_EQ(one.next(), 0xf893a2eefb32555eULL);

  SplitMix64 answer(42);
  EXPECT_EQ(answer.next(), 0xbdd732262feb6e95ULL);
  EXPECT_EQ(answer.next(), 0x28efe333b266f103ULL);
  EXPECT_EQ(answer.next(), 0x47526757130f9f52ULL);
}

TEST(SplitMix, UnitDrawsStayInRange) {
  SplitMix64 rng(12345);
  for (int i = 0; i < 10000; ++i) {
    const double value = rng.next_unit();
    ASSERT_GE(value, 0.0) << "draw " << i;
    ASSERT_LT(value, 1.0) << "draw " << i;
  }
}

TEST(SplitMix, BoundedDrawsStayInRangeAndCoverIt) {
  SplitMix64 rng(7);
  std::vector<int> seen(5, 0);
  for (int i = 0; i < 2000; ++i) {
    const std::uint64_t value = rng.next_below(5);
    ASSERT_LT(value, std::uint64_t{5}) << "draw " << i;
    seen[value] = 1;
  }
  for (std::size_t i = 0; i < seen.size(); ++i) {
    EXPECT_EQ(seen[i], 1) << "value " << i << " never drawn";
  }
  SplitMix64 single(7);
  EXPECT_EQ(single.next_below(1), std::uint64_t{0});
}

// --- repeatability ---------------------------------------------------------

TEST(Generator, SameParametersProduceTheSameGraph) {
  const Graph first = route::generate(params(7, 5, 20250904));
  const Graph second = route::generate(params(7, 5, 20250904));

  ASSERT_EQ(first.node_count(), second.node_count());
  ASSERT_EQ(first.arc_count(), second.arc_count());
  for (std::uint32_t i = 0; i < first.node_count(); ++i) {
    const route::NodeId node = route::make_node_id(i);
    EXPECT_EQ(first.coordinate(node).x, second.coordinate(node).x) << "node " << i;
    EXPECT_EQ(first.coordinate(node).y, second.coordinate(node).y) << "node " << i;
    EXPECT_EQ(first.external_id(node), second.external_id(node)) << "node " << i;

    const std::span<const Arc> a = first.arcs_from(node);
    const std::span<const Arc> b = second.arcs_from(node);
    ASSERT_EQ(a.size(), b.size()) << "node " << i;
    for (std::size_t k = 0; k < a.size(); ++k) {
      EXPECT_EQ(a[k].target, b[k].target) << "node " << i << " arc " << k;
      EXPECT_EQ(a[k].weight, b[k].weight) << "node " << i << " arc " << k;
    }
  }
}

TEST(Generator, SameParametersProduceByteIdenticalCsv) {
  const auto first = to_csv(route::generate(params(6, 4, 99)));
  const auto second = to_csv(route::generate(params(6, 4, 99)));
  EXPECT_EQ(first.first, second.first);
  EXPECT_EQ(first.second, second.second);
}

TEST(Generator, DifferentSeedsProduceDifferentCoordinates) {
  const auto a = to_csv(route::generate(params(6, 4, 1)));
  const auto b = to_csv(route::generate(params(6, 4, 2)));
  EXPECT_NE(a.first, b.first);
}

TEST(Generator, DiagonalRateDoesNotShiftTheCoordinateStream) {
  // Coordinates are drawn before any diagonal decision, so changing the rate
  // must leave every node exactly where it was.
  GeneratorParams none = params(6, 4, 5);
  none.diagonal_rate = 0.0;
  GeneratorParams all = params(6, 4, 5);
  all.diagonal_rate = 1.0;

  const Graph a = route::generate(none);
  const Graph b = route::generate(all);
  ASSERT_EQ(a.node_count(), b.node_count());
  for (std::uint32_t i = 0; i < a.node_count(); ++i) {
    const route::NodeId node = route::make_node_id(i);
    EXPECT_EQ(a.coordinate(node).x, b.coordinate(node).x) << "node " << i;
    EXPECT_EQ(a.coordinate(node).y, b.coordinate(node).y) << "node " << i;
  }
}

// --- shape -----------------------------------------------------------------

TEST(Generator, LatticeCountsMatchTheGeometry) {
  for (const auto& size :
       {std::pair<std::uint32_t, std::uint32_t>{1, 1}, {1, 6}, {6, 1}, {2, 2}, {7, 5}}) {
    GeneratorParams p = params(size.first, size.second, 3);
    p.diagonal_rate = 0.0;
    const Graph graph = route::generate(p);
    const std::string context = std::to_string(size.first) + "x" + std::to_string(size.second);

    EXPECT_EQ(graph.node_count(), std::size_t{size.first} * size.second) << context;
    EXPECT_EQ(graph.arc_count(), backbone_arcs(size.first, size.second)) << context;
  }
}

TEST(Generator, EveryDiagonalIsPresentAtRateOne) {
  GeneratorParams p = params(7, 5, 3);
  p.diagonal_rate = 1.0;
  const Graph graph = route::generate(p);

  const std::uint64_t cells = std::uint64_t{7 - 1} * (5 - 1);
  EXPECT_EQ(graph.arc_count(), backbone_arcs(7, 5) + 4 * cells);
}

TEST(Generator, ExternalIdsAreRowMajorStartingAtOne) {
  const Graph graph = route::generate(params(4, 3, 11));
  for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
    EXPECT_EQ(graph.external_id(route::make_node_id(i)), ExternalId{i} + 1) << "node " << i;
  }
}

TEST(Generator, ZeroJitterPutsNodesExactlyOnTheLattice) {
  GeneratorParams p = params(4, 3, 11);
  p.jitter = 0.0;
  p.spacing = 250.0;
  const Graph graph = route::generate(p);

  for (std::uint32_t row = 0; row < 3; ++row) {
    for (std::uint32_t col = 0; col < 4; ++col) {
      const Coord coordinate = graph.coordinate(route::make_node_id(row * 4 + col));
      EXPECT_DOUBLE_EQ(coordinate.x, col * 250.0) << row << "," << col;
      EXPECT_DOUBLE_EQ(coordinate.y, row * 250.0) << row << "," << col;
    }
  }
}

TEST(Generator, CoordinatesAndWeightsAreFinite) {
  const Graph graph = route::generate(params(9, 7, 4242));
  for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
    const route::NodeId node = route::make_node_id(i);
    ASSERT_TRUE(std::isfinite(graph.coordinate(node).x)) << "node " << i;
    ASSERT_TRUE(std::isfinite(graph.coordinate(node).y)) << "node " << i;
    for (const Arc& arc : graph.arcs_from(node)) {
      ASSERT_TRUE(std::isfinite(arc.weight)) << "node " << i;
      ASSERT_GT(arc.weight, 0.0) << "node " << i;
    }
  }
}

// --- the metric contract ---------------------------------------------------

TEST(Generator, EveryWeightIsExactlyTheEuclideanDistance) {
  const Graph graph = route::generate(params(8, 6, 777));
  for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
    const route::NodeId node = route::make_node_id(i);
    const Coord from = graph.coordinate(node);
    for (const Arc& arc : graph.arcs_from(node)) {
      const Coord to = graph.coordinate(arc.target);
      EXPECT_EQ(arc.weight, std::hypot(to.x - from.x, to.y - from.y))
          << "arc " << i << " -> " << route::index_of(arc.target);
    }
  }
}

TEST(Generator, MetricContractHoldsForEveryShapeAndSeed) {
  for (std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{123456789}}) {
    for (const auto& size :
         {std::pair<std::uint32_t, std::uint32_t>{1, 1}, {3, 1}, {1, 3}, {5, 4}, {9, 9}}) {
      const Graph graph = route::generate(params(size.first, size.second, seed));
      EXPECT_TRUE(graph.metric_report().passes)
          << size.first << "x" << size.second << " seed " << seed;
      EXPECT_EQ(graph.metric_report().violating_arcs, std::uint64_t{0});
    }
  }
}

// --- connectivity ----------------------------------------------------------

TEST(Generator, TheBackboneKeepsEveryNodeReachableInBothDirections) {
  GeneratorParams p = params(8, 6, 31337);
  p.diagonal_rate = 0.0;  // the backbone alone must be enough
  const Graph graph = route::generate(p);
  const route::NodeId origin = route::make_node_id(0);

  for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
    const route::NodeId node = route::make_node_id(i);
    EXPECT_TRUE(route::dijkstra(graph, origin, node).found()) << "0 cannot reach " << i;
    EXPECT_TRUE(route::dijkstra(graph, node, origin).found()) << i << " cannot reach 0";
  }
}

// --- CSV round trip --------------------------------------------------------

TEST(Generator, WrittenCsvReloadsIntoAnIdenticalGraph) {
  const Graph original = route::generate(params(7, 5, 2024));
  const auto text = to_csv(original);

  std::istringstream nodes(text.first);
  std::istringstream edges(text.second);
  const Graph reloaded = route::load_graph(nodes, "nodes.csv", edges, "edges.csv");

  ASSERT_EQ(reloaded.node_count(), original.node_count());
  ASSERT_EQ(reloaded.arc_count(), original.arc_count());
  EXPECT_TRUE(reloaded.metric_report().passes);

  for (std::uint32_t i = 0; i < original.node_count(); ++i) {
    const route::NodeId node = route::make_node_id(i);
    EXPECT_EQ(reloaded.coordinate(node).x, original.coordinate(node).x) << "node " << i;
    EXPECT_EQ(reloaded.coordinate(node).y, original.coordinate(node).y) << "node " << i;
    EXPECT_EQ(reloaded.external_id(node), original.external_id(node)) << "node " << i;

    const std::span<const Arc> a = original.arcs_from(node);
    const std::span<const Arc> b = reloaded.arcs_from(node);
    ASSERT_EQ(a.size(), b.size()) << "node " << i;
    for (std::size_t k = 0; k < a.size(); ++k) {
      EXPECT_EQ(a[k].target, b[k].target) << "node " << i << " arc " << k;
      EXPECT_EQ(a[k].weight, b[k].weight) << "node " << i << " arc " << k;
    }
  }
}

TEST(Generator, WrittenCsvUsesTheDocumentedHeaders) {
  const auto text = to_csv(route::generate(params(2, 2, 1)));
  EXPECT_TRUE(text.first.starts_with("id,x,y\n"));
  EXPECT_TRUE(text.second.starts_with("source,target,weight\n"));
}

// --- parameter validation --------------------------------------------------

TEST(Generator, RejectsInvalidParameters) {
  const double infinity = std::numeric_limits<double>::infinity();
  const double quiet_nan = std::numeric_limits<double>::quiet_NaN();

  std::vector<GeneratorParams> bad;
  bad.push_back(params(0, 4, 1));
  bad.push_back(params(4, 0, 1));

  GeneratorParams spacing_zero = params(4, 4, 1);
  spacing_zero.spacing = 0.0;
  bad.push_back(spacing_zero);
  GeneratorParams spacing_negative = params(4, 4, 1);
  spacing_negative.spacing = -1.0;
  bad.push_back(spacing_negative);
  GeneratorParams spacing_nan = params(4, 4, 1);
  spacing_nan.spacing = quiet_nan;
  bad.push_back(spacing_nan);

  GeneratorParams jitter_high = params(4, 4, 1);
  jitter_high.jitter = 0.5;
  bad.push_back(jitter_high);
  GeneratorParams jitter_negative = params(4, 4, 1);
  jitter_negative.jitter = -0.01;
  bad.push_back(jitter_negative);
  GeneratorParams jitter_inf = params(4, 4, 1);
  jitter_inf.jitter = infinity;
  bad.push_back(jitter_inf);

  GeneratorParams rate_low = params(4, 4, 1);
  rate_low.diagonal_rate = -0.01;
  bad.push_back(rate_low);
  GeneratorParams rate_high = params(4, 4, 1);
  rate_high.diagonal_rate = 1.01;
  bad.push_back(rate_high);

  // width * height overflows the supported node count.
  bad.push_back(params(100000, 100000, 1));

  for (std::size_t i = 0; i < bad.size(); ++i) {
    EXPECT_FALSE(route::validate_generator_params(bad[i]).empty()) << "case " << i;
    EXPECT_THROW((void)route::generate(bad[i]), GraphValidationError) << "case " << i;
  }
}

TEST(Generator, AcceptsTheDocumentedDefaultsAndBoundaries) {
  EXPECT_TRUE(route::validate_generator_params(params(1, 1, 0)).empty());

  GeneratorParams boundaries = params(3, 3, 0);
  boundaries.jitter = 0.0;
  boundaries.diagonal_rate = 0.0;
  EXPECT_TRUE(route::validate_generator_params(boundaries).empty());

  boundaries.diagonal_rate = 1.0;
  boundaries.jitter = 0.49999;
  EXPECT_TRUE(route::validate_generator_params(boundaries).empty());
}

}  // namespace
