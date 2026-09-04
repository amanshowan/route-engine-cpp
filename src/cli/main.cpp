#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "route/csv.hpp"
#include "route/errors.hpp"
#include "route/graph.hpp"
#include "route/metric.hpp"
#include "route/search.hpp"

namespace {

// Exit codes, as documented in the README.
constexpr int kExitSuccess = 0;
constexpr int kExitNoRoute = 1;
constexpr int kExitUsage = 2;
constexpr int kExitData = 3;
constexpr int kExitHeuristicContract = 4;

void print_usage(std::ostream& out) {
  out << "route-engine -- C++ route and network engine\n"
      << "\n"
      << "Usage:\n"
      << "  route-engine validate --nodes <nodes.csv> --edges <edges.csv>\n"
      << "  route-engine route    --nodes <nodes.csv> --edges <edges.csv>\n"
      << "                        --from <external-id> --to <external-id>\n"
      << "                        --algorithm dijkstra|astar-zero|astar-euclidean\n"
      << "\n"
      << "Planned (not implemented yet): generate, benchmark.\n"
      << "\n"
      << "Exit codes:\n"
      << "  0  success; route found where applicable\n"
      << "  1  valid query, but no route exists\n"
      << "  2  command-line usage error\n"
      << "  3  CSV or graph data error\n"
      << "  4  Euclidean A* requested on a graph that violates the metric contract\n";
}

/// Minimal `--key value` parser for a small, fixed option set.
class Options {
 public:
  /// Returns an error message, or an empty string on success.
  [[nodiscard]] std::string parse(const std::vector<std::string_view>& arguments) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      const std::string_view key = arguments[i];
      if (!key.starts_with("--")) {
        return "unexpected argument \"" + std::string(key) + "\"";
      }
      std::string* slot = slot_for(key);
      if (slot == nullptr) {
        return "unknown option \"" + std::string(key) + "\"";
      }
      if (i + 1 >= arguments.size()) {
        return "option \"" + std::string(key) + "\" requires a value";
      }
      if (!slot->empty()) {
        return "option \"" + std::string(key) + "\" was given more than once";
      }
      *slot = std::string(arguments[++i]);
      if (slot->empty()) {
        return "option \"" + std::string(key) + "\" requires a non-empty value";
      }
    }
    return {};
  }

  std::string nodes;
  std::string edges;
  std::string from;
  std::string to;
  std::string algorithm;

 private:
  [[nodiscard]] std::string* slot_for(std::string_view key) {
    if (key == "--nodes") {
      return &nodes;
    }
    if (key == "--edges") {
      return &edges;
    }
    if (key == "--from") {
      return &from;
    }
    if (key == "--to") {
      return &to;
    }
    if (key == "--algorithm") {
      return &algorithm;
    }
    return nullptr;
  }
};

[[nodiscard]] bool parse_external_id(std::string_view text, route::ExternalId& value) {
  const char* first = text.data();
  const char* last = text.data() + text.size();
  const std::from_chars_result result = std::from_chars(first, last, value, 10);
  return result.ec == std::errc{} && result.ptr == last;
}

/// Resolves a file identifier to its dense internal identifier.
///
/// A linear scan: the CLI performs at most two lookups per run, so an index
/// would cost more to build than it saves.
[[nodiscard]] std::optional<route::NodeId> find_node(const route::Graph& graph,
                                                     route::ExternalId external_id) {
  for (std::uint32_t i = 0; i < graph.node_count(); ++i) {
    const route::NodeId id = route::make_node_id(i);
    if (graph.external_id(id) == external_id) {
      return id;
    }
  }
  return std::nullopt;
}

int usage_error(const std::string& message) {
  std::cerr << "route-engine: " << message << "\n\n";
  print_usage(std::cerr);
  return kExitUsage;
}

void print_metric_report(std::ostream& out, const route::Graph& graph) {
  const route::MetricReport& report = graph.metric_report();
  out << "metric contract:  " << (report.passes ? "satisfied" : "VIOLATED") << "\n";
  if (report.passes) {
    out << "  every arc weight is at least the straight-line distance between its endpoints,\n"
        << "  so Euclidean A* is admissible on this graph\n";
  } else {
    out << "  violating arcs: " << report.violating_arcs << "\n"
        << "  first violation: node " << graph.external_id(report.first_violation_source) << " -> "
        << graph.external_id(report.first_violation_target) << "\n"
        << "  Euclidean A* will be refused on this graph; dijkstra and astar-zero remain "
           "available\n";
  }
  if (report.worst_ratio == std::numeric_limits<double>::infinity()) {
    out << "  worst weight/distance ratio: n/a (no arc with a positive distance)\n";
  } else {
    out << "  worst weight/distance ratio: " << report.worst_ratio << "\n";
  }
}

int command_validate(const Options& options) {
  if (options.nodes.empty() || options.edges.empty()) {
    return usage_error("validate requires --nodes and --edges");
  }
  if (!options.from.empty() || !options.to.empty() || !options.algorithm.empty()) {
    return usage_error("validate does not accept --from, --to or --algorithm");
  }

  const route::Graph graph = route::load_graph(options.nodes, options.edges);

  std::cout << std::setprecision(10);
  std::cout << "nodes file:       " << options.nodes << "\n"
            << "edges file:       " << options.edges << "\n"
            << "nodes:            " << graph.node_count() << "\n"
            << "arcs:             " << graph.arc_count() << "\n";
  print_metric_report(std::cout, graph);
  return kExitSuccess;
}

void print_stats(std::ostream& out, const route::SearchStats& stats) {
  out << "statistics:\n"
      << "  nodes expanded:   " << stats.nodes_expanded << "\n"
      << "  arcs examined:    " << stats.arcs_examined << "\n"
      << "  relaxations:      " << stats.relaxations << "\n"
      << "  queue pushes:     " << stats.pq_pushes << "\n"
      << "  queue pops:       " << stats.pq_pops << "\n"
      << "  stale pops:       " << stats.stale_pops << "\n"
      << "  max queue size:   " << stats.max_queue_size << "\n";
}

int command_route(const Options& options) {
  if (options.nodes.empty() || options.edges.empty() || options.from.empty() ||
      options.to.empty() || options.algorithm.empty()) {
    return usage_error("route requires --nodes, --edges, --from, --to and --algorithm");
  }

  const std::optional<route::Algorithm> algorithm = route::parse_algorithm(options.algorithm);
  if (!algorithm.has_value()) {
    return usage_error("unknown algorithm \"" + options.algorithm +
                       "\"; expected dijkstra, astar-zero or astar-euclidean");
  }

  route::ExternalId from_id = 0;
  route::ExternalId to_id = 0;
  if (!parse_external_id(options.from, from_id)) {
    return usage_error("--from must be a decimal unsigned integer, found \"" + options.from + "\"");
  }
  if (!parse_external_id(options.to, to_id)) {
    return usage_error("--to must be a decimal unsigned integer, found \"" + options.to + "\"");
  }

  const route::Graph graph = route::load_graph(options.nodes, options.edges);

  const std::optional<route::NodeId> source = find_node(graph, from_id);
  if (!source.has_value()) {
    std::cerr << "route-engine: node id " << from_id << " is not in " << options.nodes << "\n";
    return kExitData;
  }
  const std::optional<route::NodeId> target = find_node(graph, to_id);
  if (!target.has_value()) {
    std::cerr << "route-engine: node id " << to_id << " is not in " << options.nodes << "\n";
    return kExitData;
  }

  const route::SearchResult result = route::run_search(graph, *source, *target, algorithm.value());

  std::cout << std::setprecision(10);
  std::cout << "algorithm:        " << route::algorithm_name(algorithm.value()) << "\n"
            << "from:             " << from_id << "\n"
            << "to:               " << to_id << "\n";

  if (!result.found()) {
    std::cout << "route:            not found\n";
    print_stats(std::cout, result.stats);
    return kExitNoRoute;
  }

  std::cout << "route:            found\n"
            << "total cost:       " << result.cost << "\n"
            << "nodes on path:    " << result.path.size() << "\n"
            << "path:             ";
  for (std::size_t i = 0; i < result.path.size(); ++i) {
    if (i != 0) {
      std::cout << " -> ";
    }
    std::cout << graph.external_id(result.path[i]);
  }
  std::cout << "\n";
  print_stats(std::cout, result.stats);
  return kExitSuccess;
}

int run(const std::vector<std::string_view>& arguments) {
  if (arguments.empty()) {
    return usage_error("no subcommand given");
  }
  const std::string_view command = arguments.front();
  if (command == "--help" || command == "-h" || command == "help") {
    print_usage(std::cout);
    return kExitSuccess;
  }
  if (command == "generate" || command == "benchmark") {
    return usage_error("subcommand \"" + std::string(command) +
                       "\" is planned but not implemented yet");
  }
  if (command != "validate" && command != "route") {
    return usage_error("unknown subcommand \"" + std::string(command) + "\"");
  }

  Options options;
  const std::string error =
      options.parse(std::vector<std::string_view>(arguments.begin() + 1, arguments.end()));
  if (!error.empty()) {
    return usage_error(error);
  }
  return command == "validate" ? command_validate(options) : command_route(options);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i) {
    arguments.emplace_back(argv[i]);
  }

  try {
    return run(arguments);
  } catch (const route::GraphLoadError& error) {
    std::cerr << "route-engine: " << error.what() << "\n";
    return kExitData;
  } catch (const route::HeuristicContractError& error) {
    std::cerr << "route-engine: " << error.what() << "\n";
    return kExitHeuristicContract;
  } catch (const route::GraphValidationError& error) {
    std::cerr << "route-engine: " << error.what() << "\n";
    return kExitData;
  } catch (const std::exception& error) {
    std::cerr << "route-engine: unexpected error: " << error.what() << "\n";
    return kExitData;
  }
}
