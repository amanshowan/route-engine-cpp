#pragma once

#include <stdexcept>
#include <string>

namespace route {

/// Raised by the graph core when a graph cannot be built as requested.
///
/// This is a core-level error and carries no notion of a file or a line. The
/// CSV loader added in a later commit is responsible for catching it and
/// re-reporting it with file and line context.
class GraphValidationError : public std::runtime_error {
 public:
  explicit GraphValidationError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace route
