#pragma once

#include <string>
#include <string_view>
#include <vector>

// The TOML subset ssg reads: comments, [table] and [nested.table] headers, bare, quoted and
// dotted keys, "basic" and 'literal' strings, true/false, decimal integers, and arrays of
// those (nestable, multi-line, trailing comma ok). Floats, dates, multi-line strings, inline
// tables and arrays of tables are rejected with an error naming the line.

struct TomlValue {
  enum Kind { String, Bool, Int, Array } kind = String;
  std::string str;
  bool boolean = false;
  long long integer = 0;
  std::vector<TomlValue> array;
};

struct TomlEntry {
  std::vector<std::string> path;  // full key path: table names, then the dotted key parts
  TomlValue value;
  int line = 0;
};

struct TomlTable {
  std::vector<std::string> path;  // as written in the [header]
  int line = 0;
};

struct TomlDoc {
  std::vector<TomlEntry> entries;  // in file order
  std::vector<TomlTable> tables;   // explicit [headers], in file order
};

// `name` prefixes error messages ("site.toml:3: ..."). Throws std::runtime_error.
TomlDoc parse_toml(std::string_view text, const std::string& name);

const char* toml_kind_name(TomlValue::Kind k);
// "a.b.c", quoting parts that aren't bare keys.
std::string toml_path_string(const std::vector<std::string>& path);
