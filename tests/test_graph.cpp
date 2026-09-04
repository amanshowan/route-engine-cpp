#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "route/builder.hpp"
#include "route/graph.hpp"
#include "route/types.hpp"

namespace {

using route::Arc;
using route::Coord;
using route::ExternalId;
using route::Graph;
using route::GraphBuilder;
using route::GraphValidationError;
using route::NodeId;
using route::Weight;

constexpr NodeId n(std::uint32_t index) { return route::make_node_id(index); }

/// A builder with `count` nodes placed one metre apart along the x axis. The
/// external identifier of node i is 100 + i, so a test that confuses internal
/// and external identifiers fails instead of accidentally agreeing.
GraphBuilder make_builder(std::uint32_t count) {
  GraphBuilder builder(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    builder.set_node(n(i), Coord{static_cast<double>(i), 0.0}, ExternalId{100} + i);
  }
  return builder;
}

// --- structure -------------------------------------------------------------

TEST(Graph, EmptyGraphHasNoNodesOrArcs) {
  GraphBuilder builder(0);
  const Graph graph = builder.build();

  EXPECT_EQ(graph.node_count(), std::size_t{0});
  EXPECT_EQ(graph.arc_count(), std::size_t{0});
  EXPECT_FALSE(graph.contains(n(0)));
}

TEST(Graph, OneNodeGraphHasNoOutgoingArcs) {
  GraphBuilder builder(1);
  builder.set_node(n(0), Coord{2.0, -3.0}, ExternalId{77});
  const Graph graph = builder.build();

  EXPECT_EQ(graph.node_count(), std::size_t{1});
  EXPECT_EQ(graph.arc_count(), std::size_t{0});
  EXPECT_TRUE(graph.contains(n(0)));
  EXPECT_TRUE(graph.arcs_from(n(0)).empty());
}

TEST(Graph, StoresCoordinatesAndExternalIdentifiers) {
  GraphBuilder builder(2);
  builder.set_node(n(0), Coord{1.5, -2.5}, ExternalId{4000000000000000001ULL});
  builder.set_node(n(1), Coord{10.0, 20.0}, ExternalId{7});
  const Graph graph = builder.build();

  EXPECT_DOUBLE_EQ(graph.coordinate(n(0)).x, 1.5);
  EXPECT_DOUBLE_EQ(graph.coordinate(n(0)).y, -2.5);
  EXPECT_DOUBLE_EQ(graph.coordinate(n(1)).x, 10.0);
  EXPECT_DOUBLE_EQ(graph.coordinate(n(1)).y, 20.0);
  EXPECT_EQ(graph.external_id(n(0)), ExternalId{4000000000000000001ULL});
  EXPECT_EQ(graph.external_id(n(1)), ExternalId{7});
}

TEST(Graph, ArcsAreDirected) {
  GraphBuilder builder = make_builder(2);
  builder.add_arc(n(0), n(1), 5.0);
  const Graph graph = builder.build();

  ASSERT_EQ(graph.arcs_from(n(0)).size(), std::size_t{1});
  EXPECT_EQ(graph.arcs_from(n(0))[0].target, n(1));
  EXPECT_DOUBLE_EQ(graph.arcs_from(n(0))[0].weight, 5.0);
  EXPECT_TRUE(graph.arcs_from(n(1)).empty());
}

TEST(Graph, BidirectionalConnectionIsTwoArcs) {
  GraphBuilder builder = make_builder(2);
  builder.add_arc(n(0), n(1), 5.0);
  builder.add_arc(n(1), n(0), 5.0);
  const Graph graph = builder.build();

  EXPECT_EQ(graph.arc_count(), std::size_t{2});
  EXPECT_EQ(graph.arcs_from(n(0)).size(), std::size_t{1});
  EXPECT_EQ(graph.arcs_from(n(1)).size(), std::size_t{1});
}

TEST(Graph, NodeAndArcCountsMatchInput) {
  GraphBuilder builder = make_builder(4);
  builder.add_arc(n(0), n(1), 1.0);
  builder.add_arc(n(1), n(2), 1.0);
  builder.add_arc(n(2), n(3), 1.0);
  builder.add_arc(n(3), n(0), 3.0);
  builder.add_arc(n(0), n(3), 3.0);
  const Graph graph = builder.build();

  EXPECT_EQ(graph.node_count(), std::size_t{4});
  EXPECT_EQ(graph.arc_count(), std::size_t{5});
}

TEST(Graph, CsrRowsPartitionTheArcArray) {
  GraphBuilder builder = make_builder(4);
  builder.add_arc(n(2), n(3), 1.0);
  builder.add_arc(n(0), n(1), 1.0);
  builder.add_arc(n(0), n(2), 2.0);
  builder.add_arc(n(3), n(0), 4.0);
  const Graph graph = builder.build();

  // Rows must be contiguous, in node order, and cover every arc exactly once.
  const Arc* expected_start = graph.arcs_from(n(0)).data();
  std::size_t total = 0;
  for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
    const std::span<const Arc> row = graph.arcs_from(n(i));
    EXPECT_EQ(row.data(), expected_start) << "row " << i << " does not begin at the end of the "
                                          << "preceding row";
    expected_start = row.data() + row.size();
    total += row.size();
  }
  EXPECT_EQ(total, graph.arc_count());

  EXPECT_EQ(graph.arcs_from(n(0)).size(), std::size_t{2});
  EXPECT_EQ(graph.arcs_from(n(1)).size(), std::size_t{0});
  EXPECT_EQ(graph.arcs_from(n(2)).size(), std::size_t{1});
  EXPECT_EQ(graph.arcs_from(n(3)).size(), std::size_t{1});
}

// --- canonical ordering ----------------------------------------------------

TEST(Graph, CanonicalOrderIsIndependentOfInsertionOrder) {
  struct Spec {
    std::uint32_t source;
    std::uint32_t target;
    Weight weight;
  };
  const Spec specs[] = {
      {0, 2, 7.0}, {0, 1, 4.0}, {2, 0, 1.0}, {0, 1, 2.0}, {1, 2, 9.0}, {0, 0, 0.0},
  };

  GraphBuilder forward = make_builder(3);
  for (const Spec& spec : specs) {
    forward.add_arc(n(spec.source), n(spec.target), spec.weight);
  }
  const Graph a = forward.build();

  GraphBuilder reverse = make_builder(3);
  for (std::size_t i = std::size(specs); i > 0; --i) {
    const Spec& spec = specs[i - 1];
    reverse.add_arc(n(spec.source), n(spec.target), spec.weight);
  }
  const Graph b = reverse.build();

  ASSERT_EQ(a.arc_count(), b.arc_count());
  for (std::uint32_t i = 0; i < a.node_count(); ++i) {
    const std::span<const Arc> row_a = a.arcs_from(n(i));
    const std::span<const Arc> row_b = b.arcs_from(n(i));
    ASSERT_EQ(row_a.size(), row_b.size()) << "row " << i;
    for (std::size_t k = 0; k < row_a.size(); ++k) {
      EXPECT_EQ(row_a[k].target, row_b[k].target) << "row " << i << " arc " << k;
      EXPECT_DOUBLE_EQ(row_a[k].weight, row_b[k].weight) << "row " << i << " arc " << k;
    }
  }

  // Node 0's row, in canonical (target, weight) order.
  const std::span<const Arc> row0 = a.arcs_from(n(0));
  ASSERT_EQ(row0.size(), std::size_t{4});
  EXPECT_EQ(row0[0].target, n(0));
  EXPECT_DOUBLE_EQ(row0[0].weight, 0.0);
  EXPECT_EQ(row0[1].target, n(1));
  EXPECT_DOUBLE_EQ(row0[1].weight, 2.0);
  EXPECT_EQ(row0[2].target, n(1));
  EXPECT_DOUBLE_EQ(row0[2].weight, 4.0);
  EXPECT_EQ(row0[3].target, n(2));
  EXPECT_DOUBLE_EQ(row0[3].weight, 7.0);
}

TEST(Graph, ParallelArcsAreRetained) {
  GraphBuilder builder = make_builder(2);
  builder.add_arc(n(0), n(1), 9.0);
  builder.add_arc(n(0), n(1), 3.0);
  const Graph graph = builder.build();

  const std::span<const Arc> row = graph.arcs_from(n(0));
  ASSERT_EQ(row.size(), std::size_t{2});
  EXPECT_DOUBLE_EQ(row[0].weight, 3.0);
  EXPECT_DOUBLE_EQ(row[1].weight, 9.0);
}

TEST(Graph, SelfLoopIsRetained) {
  GraphBuilder builder = make_builder(1);
  builder.add_arc(n(0), n(0), 2.5);
  const Graph graph = builder.build();

  ASSERT_EQ(graph.arcs_from(n(0)).size(), std::size_t{1});
  EXPECT_EQ(graph.arcs_from(n(0))[0].target, n(0));
}

TEST(Graph, ZeroWeightArcIsRetained) {
  GraphBuilder builder = make_builder(2);
  builder.add_arc(n(0), n(1), 0.0);
  const Graph graph = builder.build();

  ASSERT_EQ(graph.arcs_from(n(0)).size(), std::size_t{1});
  EXPECT_DOUBLE_EQ(graph.arcs_from(n(0))[0].weight, 0.0);
}

// --- validation ------------------------------------------------------------

TEST(GraphBuilder, RejectsDuplicateNodeAssignment) {
  GraphBuilder builder(2);
  builder.set_node(n(0), Coord{0.0, 0.0}, ExternalId{1});
  EXPECT_THROW(builder.set_node(n(0), Coord{1.0, 1.0}, ExternalId{2}), GraphValidationError);
}

TEST(GraphBuilder, RejectsUnsetNodeAtBuild) {
  GraphBuilder builder(3);
  builder.set_node(n(0), Coord{0.0, 0.0}, ExternalId{1});
  builder.set_node(n(2), Coord{2.0, 0.0}, ExternalId{3});
  EXPECT_EQ(builder.assigned_node_count(), std::size_t{2});
  EXPECT_THROW((void)builder.build(), GraphValidationError);
}

TEST(GraphBuilder, RejectsOutOfRangeNodeAssignment) {
  GraphBuilder builder(2);
  EXPECT_THROW(builder.set_node(n(2), Coord{0.0, 0.0}, ExternalId{1}), GraphValidationError);
}

TEST(GraphBuilder, RejectsOutOfRangeArcEndpoints) {
  GraphBuilder builder = make_builder(2);
  EXPECT_THROW(builder.add_arc(n(2), n(0), 1.0), GraphValidationError);
  EXPECT_THROW(builder.add_arc(n(0), n(2), 1.0), GraphValidationError);
  EXPECT_EQ(builder.staged_arc_count(), std::size_t{0});
}

TEST(GraphBuilder, RejectsNegativeWeight) {
  GraphBuilder builder = make_builder(2);
  EXPECT_THROW(builder.add_arc(n(0), n(1), -0.5), GraphValidationError);
}

TEST(GraphBuilder, RejectsNonFiniteWeight) {
  GraphBuilder builder = make_builder(2);
  const double infinity = std::numeric_limits<double>::infinity();
  const double quiet_nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(builder.add_arc(n(0), n(1), infinity), GraphValidationError);
  EXPECT_THROW(builder.add_arc(n(0), n(1), -infinity), GraphValidationError);
  EXPECT_THROW(builder.add_arc(n(0), n(1), quiet_nan), GraphValidationError);
  EXPECT_EQ(builder.staged_arc_count(), std::size_t{0});
}

TEST(GraphBuilder, RejectsNonFiniteCoordinate) {
  const double infinity = std::numeric_limits<double>::infinity();
  const double quiet_nan = std::numeric_limits<double>::quiet_NaN();

  GraphBuilder builder(3);
  EXPECT_THROW(builder.set_node(n(0), Coord{quiet_nan, 0.0}, ExternalId{1}), GraphValidationError);
  EXPECT_THROW(builder.set_node(n(1), Coord{0.0, infinity}, ExternalId{2}), GraphValidationError);
  EXPECT_THROW(builder.set_node(n(2), Coord{-infinity, quiet_nan}, ExternalId{3}),
               GraphValidationError);
  EXPECT_EQ(builder.assigned_node_count(), std::size_t{0});
}

TEST(GraphBuilder, RejectsUseAfterBuild) {
  GraphBuilder builder = make_builder(1);
  const Graph graph = builder.build();
  EXPECT_EQ(graph.node_count(), std::size_t{1});

  EXPECT_THROW(builder.set_node(n(0), Coord{0.0, 0.0}, ExternalId{1}), GraphValidationError);
  EXPECT_THROW(builder.add_arc(n(0), n(0), 1.0), GraphValidationError);
  EXPECT_THROW((void)builder.build(), GraphValidationError);
}

// --- capacity --------------------------------------------------------------

TEST(Capacity, NodeCountAboveTheSupportedLimitIsRejected) {
  EXPECT_NO_THROW(route::validate_node_capacity(route::max_supported_node_count()));
  EXPECT_THROW(route::validate_node_capacity(route::max_supported_node_count() + 1),
               GraphValidationError);
  EXPECT_THROW(GraphBuilder(route::max_supported_node_count() + 1), GraphValidationError);
}

TEST(Capacity, ArcCountAboveCapacityIsRejected) {
  EXPECT_NO_THROW(route::validate_arc_capacity(route::max_arc_count()));
  EXPECT_THROW(route::validate_arc_capacity(route::max_arc_count() + 1), GraphValidationError);
}

TEST(Capacity, LimitsAreTheDocumentedProjectCaps) {
  // max_supported_node_count() is a conservative engine cap rather than the
  // capacity of NodeId: a 32-bit identifier can represent UINT32_MAX itself, so
  // capping the count here leaves the highest identifier value unused.
  EXPECT_EQ(route::max_supported_node_count(),
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
  EXPECT_LT(route::max_supported_node_count(),
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1);

  // max_arc_count() is exactly what a 32-bit CSR offset can represent.
  EXPECT_EQ(route::max_arc_count(),
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
}

// --- access ----------------------------------------------------------------

TEST(Graph, OutOfRangeAccessThrows) {
  GraphBuilder builder = make_builder(2);
  builder.add_arc(n(0), n(1), 1.0);
  const Graph graph = builder.build();

  EXPECT_FALSE(graph.contains(n(2)));
  EXPECT_THROW((void)graph.coordinate(n(2)), std::out_of_range);
  EXPECT_THROW((void)graph.external_id(n(2)), std::out_of_range);
  EXPECT_THROW((void)graph.arcs_from(n(2)), std::out_of_range);

  const NodeId far = n(std::numeric_limits<std::uint32_t>::max());
  EXPECT_THROW((void)graph.coordinate(far), std::out_of_range);
  EXPECT_THROW((void)graph.arcs_from(far), std::out_of_range);
}

TEST(Graph, EmptyGraphRejectsEveryAccess) {
  GraphBuilder builder(0);
  const Graph graph = builder.build();
  EXPECT_THROW((void)graph.coordinate(n(0)), std::out_of_range);
  EXPECT_THROW((void)graph.arcs_from(n(0)), std::out_of_range);
}

TEST(Graph, IsMoveOnly) {
  static_assert(!std::is_copy_constructible_v<Graph>);
  static_assert(!std::is_copy_assignable_v<Graph>);
  static_assert(std::is_nothrow_move_constructible_v<Graph>);
  static_assert(std::is_nothrow_move_assignable_v<Graph>);

  GraphBuilder builder = make_builder(2);
  builder.add_arc(n(0), n(1), 1.0);
  Graph original = builder.build();
  const Graph moved = std::move(original);

  EXPECT_EQ(moved.node_count(), std::size_t{2});
  EXPECT_EQ(moved.arc_count(), std::size_t{1});
}

}  // namespace
