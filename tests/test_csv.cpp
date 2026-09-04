#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>

#include "route/csv.hpp"
#include "route/errors.hpp"
#include "route/graph.hpp"
#include "route/types.hpp"

namespace {

using route::Arc;
using route::ExternalId;
using route::Graph;
using route::GraphLoadError;

constexpr const char* kValidNodes =
    "id,x,y\n"
    "10,0.0,0.0\n"
    "20,3.0,4.0\n"
    "30,3.0,10.0\n";

constexpr const char* kValidEdges =
    "source,target,weight\n"
    "10,20,5.0\n"
    "20,10,5.0\n"
    "20,30,6.0\n";

Graph load(const std::string& nodes, const std::string& edges) {
  std::istringstream node_stream(nodes);
  std::istringstream edge_stream(edges);
  return route::load_graph(node_stream, "nodes.csv", edge_stream, "edges.csv");
}

/// Runs a load that is expected to fail and returns the error, so a test can
/// assert on the file, the line and the message rather than only on the type.
std::optional<GraphLoadError> load_error(const std::string& nodes, const std::string& edges) {
  try {
    const Graph graph = load(nodes, edges);
    return std::nullopt;
  } catch (const GraphLoadError& error) {
    return error;
  }
}

// --- accepted input --------------------------------------------------------

TEST(Csv, LoadsAValidGraph) {
  const Graph graph = load(kValidNodes, kValidEdges);

  EXPECT_EQ(graph.node_count(), std::size_t{3});
  EXPECT_EQ(graph.arc_count(), std::size_t{3});
  EXPECT_TRUE(graph.metric_report().passes);

  // Internal identifiers follow the order of appearance in nodes.csv.
  EXPECT_EQ(graph.external_id(route::make_node_id(0)), ExternalId{10});
  EXPECT_EQ(graph.external_id(route::make_node_id(1)), ExternalId{20});
  EXPECT_EQ(graph.external_id(route::make_node_id(2)), ExternalId{30});
  EXPECT_DOUBLE_EQ(graph.coordinate(route::make_node_id(1)).x, 3.0);
  EXPECT_DOUBLE_EQ(graph.coordinate(route::make_node_id(1)).y, 4.0);
}

TEST(Csv, EachEdgeRowIsExactlyOneDirectedArc) {
  const Graph graph = load(kValidNodes,
                           "source,target,weight\n"
                           "10,20,5.0\n");

  EXPECT_EQ(graph.arc_count(), std::size_t{1});
  EXPECT_EQ(graph.arcs_from(route::make_node_id(0)).size(), std::size_t{1});
  EXPECT_TRUE(graph.arcs_from(route::make_node_id(1)).empty());
}

TEST(Csv, AcceptsCrlfBomCommentsBlankLinesAndSurroundingWhitespace) {
  const std::string nodes =
      "\xEF\xBB\xBF"
      "  id , x , y \r\n"
      "\r\n"
      "# a comment, with a comma in it\r\n"
      "  10 ,  0.0 , 0.0  \r\n"
      "\n"
      "\t20,3.0,4.0\r\n";
  const std::string edges =
      "source,target,weight\n"
      "   # indented comment\n"
      "\n"
      "10 , 20 , 5.0\n";

  const Graph graph = load(nodes, edges);
  EXPECT_EQ(graph.node_count(), std::size_t{2});
  EXPECT_EQ(graph.arc_count(), std::size_t{1});
  EXPECT_DOUBLE_EQ(graph.arcs_from(route::make_node_id(0))[0].weight, 5.0);
}

TEST(Csv, AcceptsScientificNotationAndNegativeCoordinates) {
  const Graph graph = load(
      "id,x,y\n"
      "1,-1.5e2,2E1\n"
      "2,0,0\n",
      "source,target,weight\n"
      "1,2,1e9\n");

  EXPECT_DOUBLE_EQ(graph.coordinate(route::make_node_id(0)).x, -150.0);
  EXPECT_DOUBLE_EQ(graph.coordinate(route::make_node_id(0)).y, 20.0);
  EXPECT_DOUBLE_EQ(graph.arcs_from(route::make_node_id(0))[0].weight, 1e9);
}

TEST(Csv, AcceptsHeaderOnlyFiles) {
  const Graph graph = load("id,x,y\n", "source,target,weight\n");
  EXPECT_EQ(graph.node_count(), std::size_t{0});
  EXPECT_EQ(graph.arc_count(), std::size_t{0});
  EXPECT_TRUE(graph.metric_report().passes);
}

TEST(Csv, RetainsParallelArcsSelfLoopsAndZeroWeights) {
  const Graph graph = load(kValidNodes,
                           "source,target,weight\n"
                           "10,20,9.0\n"
                           "10,20,5.0\n"
                           "10,10,0.0\n");

  const std::span<const Arc> row = graph.arcs_from(route::make_node_id(0));
  ASSERT_EQ(row.size(), std::size_t{3});
  EXPECT_EQ(row[0].target, route::make_node_id(0));  // self-loop, canonical order
  EXPECT_DOUBLE_EQ(row[0].weight, 0.0);
  EXPECT_DOUBLE_EQ(row[1].weight, 5.0);
  EXPECT_DOUBLE_EQ(row[2].weight, 9.0);
}

// --- rejected input --------------------------------------------------------

TEST(Csv, RejectsAnEmptyFile) {
  const auto error = load_error("", "source,target,weight\n");
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->file(), "nodes.csv");
  EXPECT_EQ(error->line(), std::size_t{0});
  EXPECT_NE(std::string(error->what()).find("empty"), std::string::npos);
}

TEST(Csv, RejectsAWrongHeader) {
  const auto nodes_error = load_error("id,x,z\n1,0,0\n", kValidEdges);
  ASSERT_TRUE(nodes_error.has_value());
  EXPECT_EQ(nodes_error->file(), "nodes.csv");
  EXPECT_EQ(nodes_error->line(), std::size_t{1});
  EXPECT_NE(std::string(nodes_error->what()).find("header"), std::string::npos);

  const auto edges_error = load_error(kValidNodes, "target,source,weight\n");
  ASSERT_TRUE(edges_error.has_value());
  EXPECT_EQ(edges_error->file(), "edges.csv");
  EXPECT_EQ(edges_error->line(), std::size_t{1});
}

TEST(Csv, RejectsAHeaderWithTheWrongFieldCount) {
  const auto error = load_error("id,x\n", kValidEdges);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->line(), std::size_t{1});
}

TEST(Csv, RejectsRowsWithTheWrongFieldCount) {
  const auto too_few = load_error("id,x,y\n1,0.0\n", kValidEdges);
  ASSERT_TRUE(too_few.has_value());
  EXPECT_EQ(too_few->line(), std::size_t{2});
  EXPECT_NE(std::string(too_few->what()).find("found 2"), std::string::npos);

  const auto too_many = load_error("id,x,y\n1,0.0,0.0,7\n", kValidEdges);
  ASSERT_TRUE(too_many.has_value());
  EXPECT_EQ(too_many->line(), std::size_t{2});
  EXPECT_NE(std::string(too_many->what()).find("found 4"), std::string::npos);
}

TEST(Csv, RejectsEmptyFields) {
  const auto error = load_error("id,x,y\n1,,0.0\n", kValidEdges);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->line(), std::size_t{2});
}

TEST(Csv, RejectsMalformedIntegers) {
  for (const char* row : {"1.5,0,0\n", "abc,0,0\n", "-1,0,0\n", "0x10,0,0\n", "12x,0,0\n"}) {
    const auto error = load_error(std::string("id,x,y\n") + row, kValidEdges);
    ASSERT_TRUE(error.has_value()) << "accepted malformed id: " << row;
    EXPECT_EQ(error->line(), std::size_t{2}) << row;
    EXPECT_NE(std::string(error->what()).find("id must be"), std::string::npos) << row;
  }
}

TEST(Csv, RejectsMalformedFloats) {
  for (const char* row : {"1,abc,0\n", "1,1.2.3,0\n", "1,,0\n", "1,1.0x,0\n"}) {
    const auto error = load_error(std::string("id,x,y\n") + row, kValidEdges);
    ASSERT_TRUE(error.has_value()) << "accepted malformed coordinate: " << row;
    EXPECT_EQ(error->line(), std::size_t{2}) << row;
  }
}

TEST(Csv, RejectsNonFiniteCoordinates) {
  for (const char* row : {"1,nan,0\n", "1,inf,0\n", "1,0,-inf\n"}) {
    const auto error = load_error(std::string("id,x,y\n") + row, kValidEdges);
    ASSERT_TRUE(error.has_value()) << "accepted non-finite coordinate: " << row;
    EXPECT_NE(std::string(error->what()).find("finite"), std::string::npos) << row;
  }
}

TEST(Csv, RejectsNonFiniteWeights) {
  for (const char* row : {"10,20,nan\n", "10,20,inf\n"}) {
    const auto error = load_error(kValidNodes, std::string("source,target,weight\n") + row);
    ASSERT_TRUE(error.has_value()) << "accepted non-finite weight: " << row;
    EXPECT_EQ(error->file(), "edges.csv") << row;
    EXPECT_EQ(error->line(), std::size_t{2}) << row;
    EXPECT_NE(std::string(error->what()).find("finite"), std::string::npos) << row;
  }
}

TEST(Csv, RejectsNegativeWeights) {
  const auto error = load_error(kValidNodes,
                                "source,target,weight\n"
                                "10,20,-0.5\n");
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->file(), "edges.csv");
  EXPECT_EQ(error->line(), std::size_t{2});
  EXPECT_NE(std::string(error->what()).find("negative"), std::string::npos);
}

TEST(Csv, RejectsDuplicateNodeIdsAndNamesTheFirstLine) {
  const auto error = load_error(
      "id,x,y\n"
      "10,0,0\n"
      "20,1,0\n"
      "10,2,0\n",
      "source,target,weight\n");
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->file(), "nodes.csv");
  EXPECT_EQ(error->line(), std::size_t{4});
  const std::string message(error->what());
  EXPECT_NE(message.find("duplicate node id 10"), std::string::npos);
  EXPECT_NE(message.find("line 2"), std::string::npos);
}

TEST(Csv, RejectsUnknownEdgeEndpoints) {
  const auto unknown_source = load_error(kValidNodes,
                                         "source,target,weight\n"
                                         "99,20,1.0\n");
  ASSERT_TRUE(unknown_source.has_value());
  EXPECT_EQ(unknown_source->line(), std::size_t{2});
  EXPECT_NE(std::string(unknown_source->what()).find("unknown source id 99"), std::string::npos);

  const auto unknown_target = load_error(kValidNodes,
                                         "source,target,weight\n"
                                         "10,99,1.0\n");
  ASSERT_TRUE(unknown_target.has_value());
  EXPECT_NE(std::string(unknown_target->what()).find("unknown target id 99"), std::string::npos);
}

TEST(Csv, ReportsTheFileAndLineOfTheFirstProblemOnly) {
  const auto error = load_error(
      "id,x,y\n"
      "1,0,0\n"
      "2,0,0\n"
      "3,bad,0\n"
      "4,also-bad,0\n",
      "source,target,weight\n");
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->line(), std::size_t{4});
  EXPECT_NE(std::string(error->what()).find("nodes.csv:4:"), std::string::npos);
}

TEST(Csv, LoadErrorIsDistinctFromGraphValidationError) {
  static_assert(!std::is_base_of_v<route::GraphValidationError, route::GraphLoadError>);
  static_assert(std::is_base_of_v<std::runtime_error, route::GraphLoadError>);
  static_assert(std::is_base_of_v<std::runtime_error, route::GraphValidationError>);
}

TEST(Csv, ReportsAnUnopenableFile) {
  try {
    const Graph graph = route::load_graph("does-not-exist-nodes.csv", "does-not-exist-edges.csv");
    FAIL() << "expected a GraphLoadError";
  } catch (const GraphLoadError& error) {
    EXPECT_EQ(error.line(), std::size_t{0});
    EXPECT_NE(std::string(error.what()).find("cannot be opened"), std::string::npos);
  }
}

}  // namespace
