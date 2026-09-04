#include "route/benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

#include "route/random.hpp"

namespace route {
namespace {

/// Costs are compared with a relative tolerance: the algorithms accumulate the
/// same arc weights but not necessarily in the same order, so the last bits may
/// legitimately differ.
[[nodiscard]] bool costs_agree(Weight lhs, Weight rhs) {
  if (std::isinf(lhs) || std::isinf(rhs)) {
    return std::isinf(lhs) && std::isinf(rhs);
  }
  const Weight scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
  return std::abs(lhs - rhs) <= 1e-9 * scale;
}

[[nodiscard]] std::string describe(const Graph& graph, const QueryPair& query) {
  return "query " + std::to_string(graph.external_id(query.source)) + " -> " +
         std::to_string(graph.external_id(query.target));
}

void accumulate(AlgorithmSummary& summary, const SearchStats& stats) {
  summary.nodes_expanded += stats.nodes_expanded;
  summary.arcs_examined += stats.arcs_examined;
  summary.relaxations += stats.relaxations;
  summary.pq_pushes += stats.pq_pushes;
  summary.pq_pops += stats.pq_pops;
  summary.stale_pops += stats.stale_pops;
  summary.max_queue_size = std::max(summary.max_queue_size, stats.max_queue_size);
}

[[nodiscard]] bool counters_match(const AlgorithmSummary& lhs, const AlgorithmSummary& rhs) {
  return lhs.nodes_expanded == rhs.nodes_expanded && lhs.arcs_examined == rhs.arcs_examined &&
         lhs.relaxations == rhs.relaxations && lhs.pq_pushes == rhs.pq_pushes &&
         lhs.pq_pops == rhs.pq_pops && lhs.stale_pops == rhs.stale_pops &&
         lhs.max_queue_size == rhs.max_queue_size;
}

}  // namespace

double AlgorithmSummary::median_seconds() const {
  if (repetition_seconds.empty()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  std::vector<double> sorted = repetition_seconds;
  std::sort(sorted.begin(), sorted.end());
  const std::size_t middle = sorted.size() / 2;
  if (sorted.size() % 2 == 1) {
    return sorted[middle];
  }
  return 0.5 * (sorted[middle - 1] + sorted[middle]);
}

std::vector<QueryPair> make_query_pairs(const Graph& graph, std::uint64_t seed, std::size_t count) {
  if (graph.node_count() < 2) {
    throw GraphValidationError(
        "route::make_query_pairs: a benchmark needs at least two nodes, this graph has " +
        std::to_string(graph.node_count()));
  }
  if (count == 0) {
    throw GraphValidationError("route::make_query_pairs: query count must be at least 1");
  }

  const std::uint64_t node_count = graph.node_count();
  SplitMix64 rng(seed);
  std::vector<QueryPair> queries;
  queries.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint64_t source = rng.next_below(node_count);
    const std::uint64_t offset = 1 + rng.next_below(node_count - 1);
    const std::uint64_t target = (source + offset) % node_count;
    queries.push_back(QueryPair{make_node_id(static_cast<std::uint32_t>(source)),
                                make_node_id(static_cast<std::uint32_t>(target))});
  }
  return queries;
}

BenchmarkReport run_benchmark(const Graph& graph, const std::vector<QueryPair>& queries,
                              std::size_t repetitions) {
  if (!graph.metric_report().passes) {
    throw HeuristicContractError(
        "route::run_benchmark: the graph violates the metric contract on " +
        std::to_string(graph.metric_report().violating_arcs) +
        " arc(s), so Euclidean A* cannot take part in a fair comparison");
  }
  if (queries.empty()) {
    throw GraphValidationError("route::run_benchmark: the query set is empty");
  }
  if (repetitions == 0) {
    throw GraphValidationError("route::run_benchmark: repetitions must be at least 1");
  }

  constexpr std::size_t kCount = kBenchmarkAlgorithms.size();
  BenchmarkReport report;
  report.queries = queries.size();
  report.repetitions = repetitions;

  // Warm-up pass. Excluded from every reported number, and used to check that
  // the algorithms agree -- so the comparison work never sits inside a timed
  // region.
  std::vector<SearchResult> reference;
  reference.reserve(queries.size());
  for (const QueryPair& query : queries) {
    reference.push_back(run_search(graph, query.source, query.target, kBenchmarkAlgorithms[0]));
  }
  for (std::size_t i = 0; i < queries.size(); ++i) {
    if (reference[i].found()) {
      ++report.routes_found;
    }
  }
  for (std::size_t a = 1; a < kCount; ++a) {
    for (std::size_t i = 0; i < queries.size(); ++i) {
      const SearchResult result =
          run_search(graph, queries[i].source, queries[i].target, kBenchmarkAlgorithms[a]);
      if (result.found() != reference[i].found()) {
        throw BenchmarkAgreementError(
            "route::run_benchmark: " + std::string(algorithm_name(kBenchmarkAlgorithms[a])) +
            " and " + std::string(algorithm_name(kBenchmarkAlgorithms[0])) +
            " disagree about whether a route exists for " + describe(graph, queries[i]));
      }
      if (result.found() && !costs_agree(result.cost, reference[i].cost)) {
        throw BenchmarkAgreementError(
            "route::run_benchmark: " + std::string(algorithm_name(kBenchmarkAlgorithms[a])) +
            " reports cost " + std::to_string(result.cost) + " but " +
            std::string(algorithm_name(kBenchmarkAlgorithms[0])) + " reports " +
            std::to_string(reference[i].cost) + " for " + describe(graph, queries[i]));
      }
    }
  }

  for (std::size_t a = 0; a < kCount; ++a) {
    report.algorithms[a].algorithm = kBenchmarkAlgorithms[a];
  }

  // Measured repetitions. The starting algorithm rotates by one each time, so
  // no algorithm always runs first and always pays the cold-cache cost.
  for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
    for (std::size_t step = 0; step < kCount; ++step) {
      const std::size_t slot = (repetition + step) % kCount;
      AlgorithmSummary pass;
      pass.algorithm = kBenchmarkAlgorithms[slot];

      const auto started = std::chrono::steady_clock::now();
      for (const QueryPair& query : queries) {
        const SearchResult result =
            run_search(graph, query.source, query.target, kBenchmarkAlgorithms[slot]);
        accumulate(pass, result.stats);
      }
      const auto finished = std::chrono::steady_clock::now();

      AlgorithmSummary& summary = report.algorithms[slot];
      if (repetition == 0) {
        const std::vector<double> keep = summary.repetition_seconds;
        summary = pass;
        summary.repetition_seconds = keep;
      } else if (!counters_match(summary, pass)) {
        throw BenchmarkAgreementError(
            "route::run_benchmark: " + std::string(algorithm_name(kBenchmarkAlgorithms[slot])) +
            " produced different counters in repetition " + std::to_string(repetition) +
            " than in the first repetition; the search is not deterministic");
      }
      summary.repetition_seconds.push_back(
          std::chrono::duration<double>(finished - started).count());
    }
  }

  return report;
}

}  // namespace route
