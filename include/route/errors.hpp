#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace route {

/// Raised by the graph core when a graph cannot be built as requested.
///
/// This is a core-level error and carries no notion of a file or a line. The
/// CSV loader catches it and re-reports it as a GraphLoadError carrying file
/// and line context.
class GraphValidationError : public std::runtime_error {
 public:
  explicit GraphValidationError(const std::string& message) : std::runtime_error(message) {}
};

/// Formats an input error as "<file>:<line>: <message>", or "<file>: <message>"
/// when the problem is not specific to a line.
[[nodiscard]] inline std::string format_load_message(const std::string& file, std::size_t line,
                                                     const std::string& message) {
  if (line == 0) {
    return file + ": " + message;
  }
  return file + ":" + std::to_string(line) + ": " + message;
}

/// Raised by the CSV loader for any problem with the input data.
///
/// Distinct from GraphValidationError: this type always names the file it came
/// from, and names the 1-based physical line when the problem is a property of
/// one line.
class GraphLoadError : public std::runtime_error {
 public:
  GraphLoadError(std::string file, std::size_t line, const std::string& message)
      : std::runtime_error(format_load_message(file, line, message)),
        file_(std::move(file)),
        line_(line) {}

  [[nodiscard]] const std::string& file() const noexcept { return file_; }

  /// 1-based physical line number, or 0 when the problem is not line-specific.
  [[nodiscard]] std::size_t line() const noexcept { return line_; }

 private:
  std::string file_;
  std::size_t line_;
};

/// Raised when a search is asked for a heuristic the graph cannot support.
///
/// In practice: Euclidean A* on a graph whose stored MetricReport says the
/// weight >= straight-line-distance contract is violated. Refusing is the
/// documented behaviour; returning a route that cannot be shown to be optimal
/// is not.
class HeuristicContractError : public std::runtime_error {
 public:
  explicit HeuristicContractError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace route
