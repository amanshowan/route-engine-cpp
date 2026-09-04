#pragma once

#include <filesystem>
#include <iosfwd>
#include <string_view>

#include "route/errors.hpp"
#include "route/graph.hpp"

namespace route {

/// Loads a graph from the project's two CSV files.
///
/// The format is narrow and fully specified; it is not a general CSV dialect.
///
///   nodes.csv   header exactly `id,x,y`
///               id  -- decimal unsigned integer, unique within the file
///               x,y -- finite projected planar coordinates, in metres
///
///   edges.csv   header exactly `source,target,weight`
///               source,target -- ids that must appear in nodes.csv
///               weight        -- finite, non-negative
///
/// Every edge row is exactly one directed arc; a bidirectional connection is
/// two rows. Parallel arcs, self-loops and zero weights are accepted.
///
/// Shared rules for both files:
///   * a leading UTF-8 byte order mark is stripped;
///   * LF and CRLF line endings are both accepted;
///   * spaces and tabs around a field are trimmed, and a field must not be
///     empty after trimming;
///   * a line that is empty after trimming is skipped, anywhere in the file;
///   * a line whose first non-whitespace character is `#` is skipped;
///   * the header must be the first line that is not skipped, and must match
///     exactly, including case and column order;
///   * every data row must have exactly the header's field count;
///   * numbers are parsed with std::from_chars, so parsing does not depend on
///     the process locale, and a field must be consumed in full.
///
/// Internal NodeId values are assigned in order of appearance in nodes.csv.
///
/// The first problem encountered aborts the load; no partial graph is produced.
///
/// \throws GraphLoadError naming the file and, where applicable, the 1-based
///         physical line number.
[[nodiscard]] Graph load_graph(const std::filesystem::path& nodes_path,
                               const std::filesystem::path& edges_path);

/// Stream overload, for callers that already hold the text. `nodes_name` and
/// `edges_name` are used only to label errors.
///
/// \throws GraphLoadError
[[nodiscard]] Graph load_graph(std::istream& nodes, std::string_view nodes_name,
                               std::istream& edges, std::string_view edges_name);

}  // namespace route
