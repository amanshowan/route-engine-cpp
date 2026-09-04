#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "route/benchmark.hpp"
#include "route/csv.hpp"
#include "route/errors.hpp"
#include "route/generator.hpp"
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
constexpr int kExitInternalDisagreement = 5;

void print_usage(std::ostream& out) {
  out << "route-engine -- C++ route and network engine\n"
      << "\n"
      << "Usage:\n"
      << "  route-engine validate  --nodes <nodes.csv> --edges <edges.csv>\n"
      << "\n"
      << "  route-engine route     --nodes <nodes.csv> --edges <edges.csv>\n"
      << "                         --from <external-id> --to <external-id>\n"
      << "                         --algorithm dijkstra|astar-zero|astar-euclidean\n"
      << "\n"
      << "  route-engine generate  --width <positive-int> --height <positive-int>\n"
      << "                         --seed <uint64>\n"
      << "                         --nodes <nodes.csv> --edges <edges.csv>\n"
      << "                         [--spacing <metres>]        default 100\n"
      << "                         [--jitter <0..0.5>]         default 0.35\n"
      << "                         [--diagonal-rate <0..1>]    default 0.25\n"
      << "                         [--force]                   overwrite existing files\n"
      << "\n"
      << "  route-engine benchmark --nodes <nodes.csv> --edges <edges.csv>\n"
      << "                         --queries <positive-int> --seed <uint64>\n"
      << "                         --repetitions <positive-int>\n"
      << "\n"
      << "generate never overwrites an existing file unless --force is given.\n"
      << "\n"
      << "Exit codes:\n"
      << "  0  success; route found where applicable\n"
      << "  1  valid query, but no route exists\n"
      << "  2  command-line usage error\n"
      << "  3  CSV, graph data or output-file error\n"
      << "  4  Euclidean A* requested on a graph that violates the metric contract\n"
      << "  5  internal disagreement between algorithms; no benchmark was reported\n";
}

/// Minimal `--key value` parser for a small, fixed option set.
///
/// Every option is stored as text and converted by the command that needs it,
/// so a malformed number is reported with the option name that produced it.
class Options {
 public:
  std::string nodes;
  std::string edges;
  std::string from;
  std::string to;
  std::string algorithm;
  std::string width;
  std::string height;
  std::string seed;
  std::string spacing;
  std::string jitter;
  std::string diagonal_rate;
  std::string queries;
  std::string repetitions;
  bool force = false;

  /// Returns an error message, or an empty string on success.
  [[nodiscard]] std::string parse(const std::vector<std::string_view>& arguments) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      const std::string_view key = arguments[i];
      if (!key.starts_with("--")) {
        return "unexpected argument \"" + std::string(key) + "\"";
      }
      if (key == "--force") {
        if (force) {
          return "option \"--force\" was given more than once";
        }
        force = true;
        continue;
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

  /// Returns an error message when an option was given that this subcommand
  /// does not accept.
  [[nodiscard]] std::string reject_unaccepted(std::string_view command,
                                              std::initializer_list<std::string_view> accepted,
                                              bool accepts_force) const {
    for (const Slot& slot : slots()) {
      if ((this->*slot.field).empty()) {
        continue;
      }
      bool allowed = false;
      for (const std::string_view key : accepted) {
        if (key == slot.key) {
          allowed = true;
          break;
        }
      }
      if (!allowed) {
        return std::string(command) + " does not accept \"" + std::string(slot.key) + "\"";
      }
    }
    if (force && !accepts_force) {
      return std::string(command) + " does not accept \"--force\"";
    }
    return {};
  }

 private:
  struct Slot {
    std::string_view key;
    std::string Options::* field;
  };

  static const std::array<Slot, 13>& slots() {
    static const std::array<Slot, 13> table = {{
        {"--nodes", &Options::nodes},
        {"--edges", &Options::edges},
        {"--from", &Options::from},
        {"--to", &Options::to},
        {"--algorithm", &Options::algorithm},
        {"--width", &Options::width},
        {"--height", &Options::height},
        {"--seed", &Options::seed},
        {"--spacing", &Options::spacing},
        {"--jitter", &Options::jitter},
        {"--diagonal-rate", &Options::diagonal_rate},
        {"--queries", &Options::queries},
        {"--repetitions", &Options::repetitions},
    }};
    return table;
  }

  [[nodiscard]] std::string* slot_for(std::string_view key) {
    for (const Slot& slot : slots()) {
      if (slot.key == key) {
        return &(this->*slot.field);
      }
    }
    return nullptr;
  }
};

template <typename Integer>
[[nodiscard]] bool parse_integer(std::string_view text, Integer& value) {
  const char* first = text.data();
  const char* last = text.data() + text.size();
  const std::from_chars_result result = std::from_chars(first, last, value, 10);
  return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool parse_real(std::string_view text, double& value) {
  const char* first = text.data();
  const char* last = text.data() + text.size();
  const std::from_chars_result result = std::from_chars(first, last, value);
  return result.ec == std::errc{} && result.ptr == last;
}

int usage_error(const std::string& message) {
  std::cerr << "route-engine: " << message << "\n\n";
  print_usage(std::cerr);
  return kExitUsage;
}

int data_error(const std::string& message) {
  std::cerr << "route-engine: " << message << "\n";
  return kExitData;
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

// --- validate --------------------------------------------------------------

int command_validate(const Options& options) {
  const std::string unaccepted =
      options.reject_unaccepted("validate", {"--nodes", "--edges"}, false);
  if (!unaccepted.empty()) {
    return usage_error(unaccepted);
  }
  if (options.nodes.empty() || options.edges.empty()) {
    return usage_error("validate requires --nodes and --edges");
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

// --- route -----------------------------------------------------------------

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
  const std::string unaccepted = options.reject_unaccepted(
      "route", {"--nodes", "--edges", "--from", "--to", "--algorithm"}, false);
  if (!unaccepted.empty()) {
    return usage_error(unaccepted);
  }
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
  if (!parse_integer(options.from, from_id)) {
    return usage_error("--from must be a decimal unsigned integer, found \"" + options.from + "\"");
  }
  if (!parse_integer(options.to, to_id)) {
    return usage_error("--to must be a decimal unsigned integer, found \"" + options.to + "\"");
  }

  const route::Graph graph = route::load_graph(options.nodes, options.edges);

  const std::optional<route::NodeId> source = find_node(graph, from_id);
  if (!source.has_value()) {
    return data_error("node id " + std::to_string(from_id) + " is not in " + options.nodes);
  }
  const std::optional<route::NodeId> target = find_node(graph, to_id);
  if (!target.has_value()) {
    return data_error("node id " + std::to_string(to_id) + " is not in " + options.nodes);
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

// --- generate --------------------------------------------------------------

int command_generate(const Options& options) {
  const std::string unaccepted =
      options.reject_unaccepted("generate",
                                {"--nodes", "--edges", "--width", "--height", "--seed", "--spacing",
                                 "--jitter", "--diagonal-rate"},
                                true);
  if (!unaccepted.empty()) {
    return usage_error(unaccepted);
  }
  if (options.nodes.empty() || options.edges.empty() || options.width.empty() ||
      options.height.empty() || options.seed.empty()) {
    return usage_error("generate requires --width, --height, --seed, --nodes and --edges");
  }

  route::GeneratorParams params;
  if (!parse_integer(options.width, params.width)) {
    return usage_error("--width must be a decimal unsigned integer, found \"" + options.width +
                       "\"");
  }
  if (!parse_integer(options.height, params.height)) {
    return usage_error("--height must be a decimal unsigned integer, found \"" + options.height +
                       "\"");
  }
  if (!parse_integer(options.seed, params.seed)) {
    return usage_error("--seed must be a decimal unsigned 64-bit integer, found \"" + options.seed +
                       "\"");
  }
  if (!options.spacing.empty() && !parse_real(options.spacing, params.spacing)) {
    return usage_error("--spacing must be a number, found \"" + options.spacing + "\"");
  }
  if (!options.jitter.empty() && !parse_real(options.jitter, params.jitter)) {
    return usage_error("--jitter must be a number, found \"" + options.jitter + "\"");
  }
  if (!options.diagonal_rate.empty() && !parse_real(options.diagonal_rate, params.diagonal_rate)) {
    return usage_error("--diagonal-rate must be a number, found \"" + options.diagonal_rate + "\"");
  }

  const std::string problem = route::validate_generator_params(params);
  if (!problem.empty()) {
    return usage_error(problem);
  }

  const std::filesystem::path nodes_path = std::filesystem::path(options.nodes).lexically_normal();
  const std::filesystem::path edges_path = std::filesystem::path(options.edges).lexically_normal();
  if (nodes_path == edges_path) {
    return usage_error("--nodes and --edges must name different files, both are \"" +
                       options.nodes + "\"");
  }
  if (!options.force) {
    for (const std::filesystem::path& path : {nodes_path, edges_path}) {
      std::error_code code;
      if (std::filesystem::exists(path, code)) {
        return data_error(path.string() +
                          " already exists; refusing to overwrite it, pass --force to replace it");
      }
    }
  }

  const route::Graph graph = route::generate(params);

  std::ofstream nodes_out(nodes_path);
  if (!nodes_out) {
    return data_error(nodes_path.string() + " cannot be opened for writing");
  }
  std::ofstream edges_out(edges_path);
  if (!edges_out) {
    return data_error(edges_path.string() + " cannot be opened for writing");
  }

  nodes_out << "# route-engine generated lattice: width=" << params.width
            << " height=" << params.height << " seed=" << params.seed << "\n";
  edges_out << "# route-engine generated lattice: width=" << params.width
            << " height=" << params.height << " seed=" << params.seed
            << " diagonal-rate=" << params.diagonal_rate << "\n"
            << "# every weight is exactly the Euclidean distance between its endpoints\n";
  route::write_csv(graph, nodes_out, edges_out);

  nodes_out.flush();
  edges_out.flush();
  if (!nodes_out) {
    return data_error("writing " + nodes_path.string() + " failed");
  }
  if (!edges_out) {
    return data_error("writing " + edges_path.string() + " failed");
  }

  std::cout << std::setprecision(10);
  std::cout << "generated:        " << params.width << " x " << params.height << " lattice\n"
            << "seed:             " << params.seed << "\n"
            << "spacing:          " << params.spacing << "\n"
            << "jitter:           " << params.jitter << "\n"
            << "diagonal rate:    " << params.diagonal_rate << "\n"
            << "nodes:            " << graph.node_count() << "\n"
            << "arcs:             " << graph.arc_count() << "\n"
            << "nodes file:       " << nodes_path.string() << "\n"
            << "edges file:       " << edges_path.string() << "\n";
  print_metric_report(std::cout, graph);
  return kExitSuccess;
}

// --- benchmark -------------------------------------------------------------

void print_benchmark(std::ostream& out, const route::Graph& graph,
                     const route::BenchmarkReport& report) {
  out << "\ntotals over " << report.queries << " queries, per algorithm\n";
  out << std::left << std::setw(18) << "algorithm" << std::right << std::setw(16) << "expanded"
      << std::setw(16) << "arcs" << std::setw(14) << "pushes" << std::setw(14) << "pops"
      << std::setw(12) << "stale" << std::setw(12) << "max queue" << std::setw(14) << "median s"
      << "\n";
  for (const route::AlgorithmSummary& summary : report.algorithms) {
    out << std::left << std::setw(18) << route::algorithm_name(summary.algorithm) << std::right
        << std::setw(16) << summary.nodes_expanded << std::setw(16) << summary.arcs_examined
        << std::setw(14) << summary.pq_pushes << std::setw(14) << summary.pq_pops << std::setw(12)
        << summary.stale_pops << std::setw(12) << summary.max_queue_size << std::setw(14)
        << std::fixed << std::setprecision(6) << summary.median_seconds() << std::defaultfloat
        << "\n";
  }

  const double baseline = static_cast<double>(report.algorithms[0].nodes_expanded);
  out << "\nnode expansions relative to " << route::algorithm_name(report.algorithms[0].algorithm)
      << ":\n";
  for (const route::AlgorithmSummary& summary : report.algorithms) {
    const double ratio =
        baseline > 0.0 ? static_cast<double>(summary.nodes_expanded) / baseline : 0.0;
    out << "  " << std::left << std::setw(18) << route::algorithm_name(summary.algorithm)
        << std::right << std::fixed << std::setprecision(3) << ratio << std::defaultfloat << "\n";
  }

  out << "\nNode expansions are the primary comparison. They are exact, reproducible for a\n"
      << "given graph and query set, and independent of this machine.\n"
      << "\n"
      << "Elapsed time is a secondary observation only. It describes this build on this\n"
      << "machine at this moment and is not comparable with any other machine, compiler,\n"
      << "build type or run; no test asserts anything about it. These figures also do not\n"
      << "claim that A* always expands fewer nodes than Dijkstra -- that depends on the\n"
      << "graph and on the query, and this is one graph and one query set.\n";
  static_cast<void>(graph);
}

int command_benchmark(const Options& options) {
  const std::string unaccepted = options.reject_unaccepted(
      "benchmark", {"--nodes", "--edges", "--queries", "--seed", "--repetitions"}, false);
  if (!unaccepted.empty()) {
    return usage_error(unaccepted);
  }
  if (options.nodes.empty() || options.edges.empty() || options.queries.empty() ||
      options.seed.empty() || options.repetitions.empty()) {
    return usage_error("benchmark requires --nodes, --edges, --queries, --seed and --repetitions");
  }

  std::uint64_t query_count = 0;
  std::uint64_t repetitions = 0;
  std::uint64_t seed = 0;
  if (!parse_integer(options.queries, query_count) || query_count == 0) {
    return usage_error("--queries must be a positive decimal integer, found \"" + options.queries +
                       "\"");
  }
  if (!parse_integer(options.repetitions, repetitions) || repetitions == 0) {
    return usage_error("--repetitions must be a positive decimal integer, found \"" +
                       options.repetitions + "\"");
  }
  if (!parse_integer(options.seed, seed)) {
    return usage_error("--seed must be a decimal unsigned 64-bit integer, found \"" + options.seed +
                       "\"");
  }

  const route::Graph graph = route::load_graph(options.nodes, options.edges);
  if (graph.node_count() < 2) {
    return data_error("a benchmark needs at least two nodes, " + options.nodes + " defines " +
                      std::to_string(graph.node_count()));
  }
  if (!graph.metric_report().passes) {
    std::cerr << "route-engine: this graph violates the metric contract on "
              << graph.metric_report().violating_arcs
              << " arc(s), so Euclidean A* cannot take part in a fair comparison and no\n"
                 "benchmark was run\n";
    return kExitHeuristicContract;
  }

  const std::vector<route::QueryPair> queries =
      route::make_query_pairs(graph, seed, static_cast<std::size_t>(query_count));
  const route::BenchmarkReport report =
      route::run_benchmark(graph, queries, static_cast<std::size_t>(repetitions));

  std::cout << std::setprecision(10);
  std::cout << "nodes file:       " << options.nodes << "\n"
            << "edges file:       " << options.edges << "\n"
            << "nodes:            " << graph.node_count() << "\n"
            << "arcs:             " << graph.arc_count() << "\n"
            << "metric contract:  satisfied\n"
            << "queries:          " << report.queries << " (seed " << seed << ")\n"
            << "repetitions:      " << report.repetitions << "\n"
            << "warm-up:          1 pass per algorithm, excluded from every figure below\n"
            << "order:            rotated by one algorithm on each repetition\n"
            << "routes found:     " << report.routes_found << " of " << report.queries << "\n"
            << "agreement:        every algorithm agrees on route existence and cost\n";
  print_benchmark(std::cout, graph, report);
  return kExitSuccess;
}

// --- dispatch --------------------------------------------------------------

int run(const std::vector<std::string_view>& arguments) {
  if (arguments.empty()) {
    return usage_error("no subcommand given");
  }
  const std::string_view command = arguments.front();
  if (command == "--help" || command == "-h" || command == "help") {
    print_usage(std::cout);
    return kExitSuccess;
  }
  if (command != "validate" && command != "route" && command != "generate" &&
      command != "benchmark") {
    return usage_error("unknown subcommand \"" + std::string(command) + "\"");
  }

  Options options;
  const std::string error =
      options.parse(std::vector<std::string_view>(arguments.begin() + 1, arguments.end()));
  if (!error.empty()) {
    return usage_error(error);
  }

  if (command == "validate") {
    return command_validate(options);
  }
  if (command == "route") {
    return command_route(options);
  }
  if (command == "generate") {
    return command_generate(options);
  }
  return command_benchmark(options);
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
  } catch (const route::BenchmarkAgreementError& error) {
    std::cerr << "route-engine: " << error.what() << "\n";
    return kExitInternalDisagreement;
  } catch (const route::GraphValidationError& error) {
    std::cerr << "route-engine: " << error.what() << "\n";
    return kExitData;
  } catch (const std::exception& error) {
    std::cerr << "route-engine: unexpected error: " << error.what() << "\n";
    return kExitData;
  }
}
