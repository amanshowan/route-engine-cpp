#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "route/builder.hpp"
#include "route/graph.hpp"
#include "route/metric.hpp"
#include "route/types.hpp"

namespace {

using route::Coord;
using route::ExternalId;
using route::Graph;
using route::GraphBuilder;
using route::MetricReport;
using route::NodeId;
using route::Weight;

constexpr NodeId n(std::uint32_t index) { return route::make_node_id(index); }

/// Two nodes at the given coordinates and a single arc 0 -> 1.
Graph two_node_graph(Coord from, Coord to, Weight weight) {
  GraphBuilder builder(2);
  builder.set_node(n(0), from, ExternalId{1});
  builder.set_node(n(1), to, ExternalId{2});
  builder.add_arc(n(0), n(1), weight);
  return builder.build();
}

// A 3-4-5 triangle gives an exact, easily reasoned-about distance.
constexpr Coord kOrigin{0.0, 0.0};
constexpr Coord kThreeFour{3.0, 4.0};

TEST(Metric, PassesWhenWeightExceedsDistance) {
  const Graph graph = two_node_graph(kOrigin, kThreeFour, 6.0);
  const MetricReport& report = graph.metric_report();

  EXPECT_TRUE(report.passes);
  EXPECT_EQ(report.violating_arcs, std::uint64_t{0});
  EXPECT_DOUBLE_EQ(report.worst_ratio, 6.0 / std::hypot(3.0, 4.0));
}

TEST(Metric, PassesWhenWeightEqualsDistanceExactly) {
  const double distance = std::hypot(3.0, 4.0);
  const Graph graph = two_node_graph(kOrigin, kThreeFour, distance);
  const MetricReport& report = graph.metric_report();

  EXPECT_TRUE(report.passes);
  EXPECT_EQ(report.violating_arcs, std::uint64_t{0});
  EXPECT_DOUBLE_EQ(report.worst_ratio, 1.0);
}

TEST(Metric, FailsWhenWeightIsOneUlpBelowDistance) {
  // The contract is checked against the stored values with no tolerance, so the
  // smallest representable shortfall must be reported as a violation.
  const double distance = std::hypot(3.0, 4.0);
  const double just_below = std::nextafter(distance, 0.0);
  ASSERT_LT(just_below, distance);

  const Graph graph = two_node_graph(kOrigin, kThreeFour, just_below);
  const MetricReport& report = graph.metric_report();

  EXPECT_FALSE(report.passes);
  EXPECT_EQ(report.violating_arcs, std::uint64_t{1});
  EXPECT_EQ(report.first_violation_source, n(0));
  EXPECT_EQ(report.first_violation_target, n(1));
  EXPECT_LT(report.worst_ratio, 1.0);
}

TEST(Metric, CountsEveryViolationAndReportsTheFirstInCanonicalOrder) {
  GraphBuilder builder(4);
  builder.set_node(n(0), Coord{0.0, 0.0}, ExternalId{1});
  builder.set_node(n(1), Coord{10.0, 0.0}, ExternalId{2});
  builder.set_node(n(2), Coord{0.0, 10.0}, ExternalId{3});
  builder.set_node(n(3), Coord{10.0, 10.0}, ExternalId{4});

  // Added out of canonical order; 2 -> 3 and 0 -> 1 both violate.
  builder.add_arc(n(2), n(3), 1.0);
  builder.add_arc(n(1), n(3), 40.0);
  builder.add_arc(n(0), n(1), 9.0);
  const Graph graph = builder.build();

  const MetricReport& report = graph.metric_report();
  EXPECT_FALSE(report.passes);
  EXPECT_EQ(report.violating_arcs, std::uint64_t{2});
  EXPECT_EQ(report.first_violation_source, n(0));
  EXPECT_EQ(report.first_violation_target, n(1));
  EXPECT_DOUBLE_EQ(report.worst_ratio, 0.1);
}

TEST(Metric, ZeroDistanceSelfLoopPasses) {
  GraphBuilder builder(1);
  builder.set_node(n(0), Coord{5.0, 5.0}, ExternalId{1});
  builder.add_arc(n(0), n(0), 0.0);
  const Graph graph = builder.build();

  const MetricReport& report = graph.metric_report();
  EXPECT_TRUE(report.passes);
  EXPECT_EQ(report.violating_arcs, std::uint64_t{0});
  // A zero-distance arc has no meaningful weight/distance ratio and is excluded.
  EXPECT_EQ(report.worst_ratio, std::numeric_limits<double>::infinity());
}

TEST(Metric, CoincidentEndpointsPassWithAnyNonNegativeWeight) {
  const Graph zero_weight = two_node_graph(kOrigin, kOrigin, 0.0);
  EXPECT_TRUE(zero_weight.metric_report().passes);
  EXPECT_EQ(zero_weight.metric_report().worst_ratio, std::numeric_limits<double>::infinity());

  const Graph positive_weight = two_node_graph(kOrigin, kOrigin, 12.0);
  EXPECT_TRUE(positive_weight.metric_report().passes);
  EXPECT_EQ(positive_weight.metric_report().worst_ratio, std::numeric_limits<double>::infinity());
}

TEST(Metric, WorstRatioIgnoresZeroDistanceArcs) {
  GraphBuilder builder(2);
  builder.set_node(n(0), Coord{0.0, 0.0}, ExternalId{1});
  builder.set_node(n(1), Coord{0.0, 4.0}, ExternalId{2});
  builder.add_arc(n(0), n(0), 100.0);  // zero distance, excluded from the ratio
  builder.add_arc(n(0), n(1), 8.0);    // ratio 2.0
  const Graph graph = builder.build();

  EXPECT_TRUE(graph.metric_report().passes);
  EXPECT_DOUBLE_EQ(graph.metric_report().worst_ratio, 2.0);
}

TEST(Metric, GraphWithNoArcsPassesVacuously) {
  GraphBuilder builder(3);
  builder.set_node(n(0), Coord{0.0, 0.0}, ExternalId{1});
  builder.set_node(n(1), Coord{1.0, 0.0}, ExternalId{2});
  builder.set_node(n(2), Coord{2.0, 0.0}, ExternalId{3});
  const Graph graph = builder.build();

  const MetricReport& report = graph.metric_report();
  EXPECT_TRUE(report.passes);
  EXPECT_EQ(report.violating_arcs, std::uint64_t{0});
  EXPECT_EQ(report.worst_ratio, std::numeric_limits<double>::infinity());
}

TEST(Metric, OneNodeGraphPassesVacuously) {
  GraphBuilder builder(1);
  builder.set_node(n(0), Coord{3.0, 3.0}, ExternalId{1});
  const Graph graph = builder.build();

  EXPECT_TRUE(graph.metric_report().passes);
  EXPECT_EQ(graph.metric_report().violating_arcs, std::uint64_t{0});
}

TEST(Metric, EmptyGraphPassesVacuously) {
  GraphBuilder builder(0);
  const Graph graph = builder.build();

  EXPECT_TRUE(graph.metric_report().passes);
  EXPECT_EQ(graph.metric_report().violating_arcs, std::uint64_t{0});
  EXPECT_EQ(graph.metric_report().worst_ratio, std::numeric_limits<double>::infinity());
}

TEST(Metric, ReportIsComputedOnceAndStoredOnTheGraph) {
  const Graph graph = two_node_graph(kOrigin, kThreeFour, 6.0);
  const MetricReport& first = graph.metric_report();
  const MetricReport& second = graph.metric_report();
  EXPECT_EQ(&first, &second);
}

}  // namespace
