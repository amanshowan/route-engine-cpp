#include "route/csv.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "route/builder.hpp"

namespace route {
namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::size_t kFieldCount = 3;

[[nodiscard]] std::string_view trim(std::string_view text) {
  const auto is_space = [](char c) { return c == ' ' || c == '\t'; };
  while (!text.empty() && is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

/// True for a line that carries no data: empty after trimming, or a comment.
[[nodiscard]] bool is_skippable(std::string_view trimmed) {
  return trimmed.empty() || trimmed.front() == '#';
}

/// Splits on ',' into exactly kFieldCount trimmed fields.
/// Returns false when the field count does not match; `found` reports the
/// count that was actually present.
[[nodiscard]] bool split_fields(std::string_view line,
                                std::array<std::string_view, kFieldCount>& out,
                                std::size_t& found) {
  found = 0;
  std::size_t begin = 0;
  while (true) {
    const std::size_t comma = line.find(',', begin);
    const std::string_view field =
        comma == std::string_view::npos ? line.substr(begin) : line.substr(begin, comma - begin);
    if (found < kFieldCount) {
      out[found] = trim(field);
    }
    ++found;
    if (comma == std::string_view::npos) {
      break;
    }
    begin = comma + 1;
  }
  return found == kFieldCount;
}

[[nodiscard]] bool parse_external_id(std::string_view field, ExternalId& value) {
  if (field.empty()) {
    return false;
  }
  const char* first = field.data();
  const char* last = field.data() + field.size();
  const std::from_chars_result result = std::from_chars(first, last, value, 10);
  return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool parse_number(std::string_view field, double& value) {
  if (field.empty()) {
    return false;
  }
  const char* first = field.data();
  const char* last = field.data() + field.size();
  const std::from_chars_result result = std::from_chars(first, last, value);
  return result.ec == std::errc{} && result.ptr == last;
}

/// Reads one physical line, stripping a trailing CR so that CRLF input behaves
/// exactly like LF input. Returns false at end of input.
[[nodiscard]] bool read_line(std::istream& in, std::string& line) {
  if (!std::getline(in, line)) {
    return false;
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return true;
}

class Reader {
 public:
  Reader(std::istream& in, std::string name) : in_(in), name_(std::move(name)) {}

  /// Advances to the next line that is not blank and not a comment.
  /// Returns false at end of input.
  [[nodiscard]] bool next(std::string_view& content) {
    while (read_line(in_, buffer_)) {
      ++line_number_;
      std::string_view view = buffer_;
      if (line_number_ == 1 && view.starts_with(kUtf8Bom)) {
        view.remove_prefix(kUtf8Bom.size());
      }
      view = trim(view);
      if (is_skippable(view)) {
        continue;
      }
      content = view;
      return true;
    }
    return false;
  }

  [[nodiscard]] std::size_t line_number() const noexcept { return line_number_; }

  [[noreturn]] void fail(const std::string& message) const {
    throw GraphLoadError(name_, line_number_, message);
  }

  [[noreturn]] void fail_at(std::size_t line, const std::string& message) const {
    throw GraphLoadError(name_, line, message);
  }

  /// Reads and checks the header, then returns the fields of each data row.
  void expect_header(const std::array<std::string_view, kFieldCount>& expected) {
    std::string_view header;
    if (!next(header)) {
      fail_at(0, std::string("file is empty; expected a header line \"") +
                     std::string(expected[0]) + "," + std::string(expected[1]) + "," +
                     std::string(expected[2]) + "\"");
    }
    std::array<std::string_view, kFieldCount> fields{};
    std::size_t found = 0;
    if (!split_fields(header, fields, found) || fields != expected) {
      fail("header must be exactly \"" + std::string(expected[0]) + "," + std::string(expected[1]) +
           "," + std::string(expected[2]) + "\", found \"" + std::string(header) + "\"");
    }
  }

  /// Reads the next data row. Returns false at end of input.
  [[nodiscard]] bool next_row(std::array<std::string_view, kFieldCount>& fields) {
    std::string_view line;
    if (!next(line)) {
      return false;
    }
    std::size_t found = 0;
    if (!split_fields(line, fields, found)) {
      fail("expected " + std::to_string(kFieldCount) + " comma-separated fields, found " +
           std::to_string(found));
    }
    for (const std::string_view field : fields) {
      if (field.empty()) {
        fail("fields must not be empty");
      }
    }
    return true;
  }

  /// Re-reports a core validation failure with this file's line context.
  [[noreturn]] void rethrow(const GraphValidationError& error) const {
    throw GraphLoadError(name_, line_number_, error.what());
  }

 private:
  std::istream& in_;
  std::string name_;
  std::string buffer_;
  std::size_t line_number_ = 0;
};

struct NodeRecord {
  ExternalId external_id = 0;
  Coord coordinate;
  std::size_t line = 0;
};

[[nodiscard]] std::vector<NodeRecord> read_nodes(
    std::istream& in, std::string_view name, std::unordered_map<ExternalId, std::uint32_t>& index) {
  Reader reader(in, std::string(name));
  reader.expect_header({"id", "x", "y"});

  std::vector<NodeRecord> records;
  std::array<std::string_view, kFieldCount> fields{};
  while (reader.next_row(fields)) {
    NodeRecord record;
    record.line = reader.line_number();

    if (!parse_external_id(fields[0], record.external_id)) {
      reader.fail("id must be a decimal unsigned integer, found \"" + std::string(fields[0]) +
                  "\"");
    }
    if (!parse_number(fields[1], record.coordinate.x) || !std::isfinite(record.coordinate.x)) {
      reader.fail("x must be a finite number, found \"" + std::string(fields[1]) + "\"");
    }
    if (!parse_number(fields[2], record.coordinate.y) || !std::isfinite(record.coordinate.y)) {
      reader.fail("y must be a finite number, found \"" + std::string(fields[2]) + "\"");
    }

    if (records.size() >= max_supported_node_count()) {
      reader.fail("more nodes than this engine supports (" +
                  std::to_string(max_supported_node_count()) + ")");
    }
    const auto [it, inserted] =
        index.emplace(record.external_id, static_cast<std::uint32_t>(records.size()));
    if (!inserted) {
      reader.fail("duplicate node id " + std::to_string(record.external_id) +
                  ", already defined on line " + std::to_string(records[it->second].line));
    }
    records.push_back(record);
  }
  return records;
}

void read_edges(std::istream& in, std::string_view name,
                const std::unordered_map<ExternalId, std::uint32_t>& index, GraphBuilder& builder) {
  Reader reader(in, std::string(name));
  reader.expect_header({"source", "target", "weight"});

  std::array<std::string_view, kFieldCount> fields{};
  while (reader.next_row(fields)) {
    ExternalId source_id = 0;
    ExternalId target_id = 0;
    Weight weight = 0.0;

    if (!parse_external_id(fields[0], source_id)) {
      reader.fail("source must be a decimal unsigned integer, found \"" + std::string(fields[0]) +
                  "\"");
    }
    if (!parse_external_id(fields[1], target_id)) {
      reader.fail("target must be a decimal unsigned integer, found \"" + std::string(fields[1]) +
                  "\"");
    }
    if (!parse_number(fields[2], weight) || !std::isfinite(weight)) {
      reader.fail("weight must be a finite number, found \"" + std::string(fields[2]) + "\"");
    }
    if (weight < 0.0) {
      reader.fail("weight must not be negative, found " + std::string(fields[2]));
    }

    const auto source = index.find(source_id);
    if (source == index.end()) {
      reader.fail("unknown source id " + std::to_string(source_id) +
                  "; it is not in the node file");
    }
    const auto target = index.find(target_id);
    if (target == index.end()) {
      reader.fail("unknown target id " + std::to_string(target_id) +
                  "; it is not in the node file");
    }

    try {
      builder.add_arc(make_node_id(source->second), make_node_id(target->second), weight);
    } catch (const GraphValidationError& error) {
      reader.rethrow(error);
    }
  }
}

}  // namespace

Graph load_graph(std::istream& nodes, std::string_view nodes_name, std::istream& edges,
                 std::string_view edges_name) {
  std::unordered_map<ExternalId, std::uint32_t> index;
  const std::vector<NodeRecord> records = read_nodes(nodes, nodes_name, index);

  GraphBuilder builder(records.size());
  for (std::size_t i = 0; i < records.size(); ++i) {
    const NodeRecord& record = records[i];
    try {
      builder.set_node(make_node_id(static_cast<std::uint32_t>(i)), record.coordinate,
                       record.external_id);
    } catch (const GraphValidationError& error) {
      throw GraphLoadError(std::string(nodes_name), record.line, error.what());
    }
  }

  read_edges(edges, edges_name, index, builder);

  try {
    return builder.build();
  } catch (const GraphValidationError& error) {
    throw GraphLoadError(std::string(edges_name), 0, error.what());
  }
}

Graph load_graph(const std::filesystem::path& nodes_path, const std::filesystem::path& edges_path) {
  std::ifstream nodes(nodes_path);
  if (!nodes) {
    throw GraphLoadError(nodes_path.string(), 0, "cannot be opened for reading");
  }
  std::ifstream edges(edges_path);
  if (!edges) {
    throw GraphLoadError(edges_path.string(), 0, "cannot be opened for reading");
  }
  return load_graph(nodes, nodes_path.string(), edges, edges_path.string());
}

}  // namespace route
