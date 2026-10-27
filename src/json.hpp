#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

// A small JSON value for the plugin protocol: parse one document, build one, print it on a
// single line. Objects are sorted by key, so output is deterministic.
struct Json {
  enum Kind { Null, Bool, Int, Real, String, Array, Object } kind = Null;
  bool boolean = false;
  long long integer = 0;
  double real = 0;
  std::string str;
  std::vector<Json> array;
  std::map<std::string, Json> object;

  Json() = default;
  Json(const char* s) : kind(String), str(s) {}
  Json(std::string s) : kind(String), str(std::move(s)) {}
  Json(bool b) : kind(Bool), boolean(b) {}
  Json(long long i) : kind(Int), integer(i) {}
  Json(int i) : kind(Int), integer(i) {}

  static Json make_array() { Json j; j.kind = Array; return j; }
  static Json make_object() { Json j; j.kind = Object; return j; }

  bool is_string() const { return kind == String; }
  bool is_object() const { return kind == Object; }
  bool is_array() const { return kind == Array; }
  // The member `key` of an object, or nullptr (also for a non-object).
  const Json* find(const std::string& key) const;
  // The string at `key`, or `def` if it is missing or not a string.
  std::string str_at(const std::string& key, const std::string& def = "") const;

  Json& set(const std::string& key, Json v) { object[key] = std::move(v); return *this; }
  Json& push(Json v) { array.push_back(std::move(v)); return *this; }

  std::string dump() const;
};

// Throws std::runtime_error on bad input, trailing text or nesting past 64 levels.
Json parse_json(std::string_view text);
