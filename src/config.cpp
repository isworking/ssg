#include "config.hpp"
#include "util.hpp"

#include <cctype>
#include <climits>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

// A small TOML subset: comments, [table] headers, bare or quoted keys, "basic" and 'literal'
// strings, true/false, integers, and arrays of strings (multi-line, trailing comma ok).

namespace {

struct Value {
  enum Kind { String, Bool, Int, Array } kind = String;
  std::string str;
  bool boolean = false;
  long long integer = 0;
  std::vector<std::string> array;
};

const char* kind_name(Value::Kind k) {
  switch (k) {
    case Value::String: return "a string";
    case Value::Bool: return "a boolean";
    case Value::Int: return "an integer";
    case Value::Array: return "an array";
  }
  return "?";
}

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}

  [[noreturn]] void fail(const std::string& msg) const {
    throw std::runtime_error("site.toml:" + std::to_string(line_) + ": " + msg);
  }

  bool at_end() const { return i_ >= s_.size(); }
  int line() const { return line_; }
  char peek() const { return at_end() ? '\0' : s_[i_]; }

  void expect(char c, const std::string& what) {
    if (peek() != c) fail("expected " + what);
    ++i_;
  }

  void skip_space() {
    while (!at_end() && (s_[i_] == ' ' || s_[i_] == '\t')) ++i_;
  }

  void skip_comment() {
    if (peek() == '#') {
      while (!at_end() && s_[i_] != '\n') ++i_;
    }
  }

  // Blank lines, comments, and newlines (between statements or inside arrays).
  void skip_space_lines() {
    for (;;) {
      skip_space();
      skip_comment();
      if (peek() == '\r') ++i_;
      if (peek() != '\n') return;
      ++i_;
      ++line_;
    }
  }

  void end_of_line() {
    skip_space();
    skip_comment();
    if (peek() == '\r') ++i_;
    if (at_end()) return;
    if (peek() != '\n') fail("expected end of line");
    ++i_;
    ++line_;
  }

  std::string key() {
    if (peek() == '"' || peek() == '\'') return string_value();
    const size_t start = i_;
    while (!at_end() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_' ||
                         s_[i_] == '-')) {
      ++i_;
    }
    if (i_ == start) fail("expected a key");
    std::string k(s_.substr(start, i_ - start));
    skip_space();
    if (peek() == '.') fail("dotted keys are not supported");
    return k;
  }

  std::string table_header() {
    ++i_;  // [
    skip_space();
    if (peek() == '[') fail("arrays of tables are not supported");
    std::string name = key();
    skip_space();
    if (peek() != ']') fail("expected ] after table name");
    ++i_;
    end_of_line();
    return name;
  }

  Value value() {
    Value v;
    const char c = peek();
    if (c == '"' || c == '\'') {
      v.str = string_value();
    } else if (c == '[') {
      v.kind = Value::Array;
      ++i_;
      for (;;) {
        skip_space_lines();
        if (peek() == ']') break;
        if (peek() != '"' && peek() != '\'') fail("arrays may only hold strings");
        v.array.push_back(string_value());
        skip_space_lines();
        if (peek() == ',') {
          ++i_;
          continue;
        }
        if (peek() != ']') fail("expected , or ] in array");
      }
      ++i_;
    } else if (s_.substr(i_, 4) == "true") {
      v.kind = Value::Bool;
      v.boolean = true;
      i_ += 4;
    } else if (s_.substr(i_, 5) == "false") {
      v.kind = Value::Bool;
      i_ += 5;
    } else if (c == '+' || c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      v.kind = Value::Int;
      bool neg = c == '-';
      if (c == '+' || c == '-') ++i_;
      const size_t start = i_;
      long long n = 0;
      while (!at_end() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) {
        if (s_[i_] != '_') {
          if (n > (LLONG_MAX - 9) / 10) fail("integer out of range");
          n = n * 10 + (s_[i_] - '0');
        }
        ++i_;
      }
      if (i_ == start) fail("expected a number");
      if (!at_end() && (s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == ':')) {
        fail("only integers are supported");
      }
      v.integer = neg ? -n : n;
    } else if (c == '{') {
      fail("inline tables are not supported");
    } else {
      fail("expected a value");
    }
    return v;
  }

 private:
  static void append_utf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  std::string string_value() {
    const char q = s_[i_++];
    if (s_.substr(i_, 2) == std::string_view(q == '"' ? "\"\"" : "''")) {
      fail("multi-line strings are not supported");
    }
    std::string out;
    for (;;) {
      if (at_end() || s_[i_] == '\n') fail("unterminated string");
      const char c = s_[i_++];
      if (c == q) return out;
      if (c != '\\' || q == '\'') {
        out += c;
        continue;
      }
      if (at_end()) fail("unterminated string");
      const char e = s_[i_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u':
        case 'U': {
          const size_t n = e == 'u' ? 4 : 8;
          if (i_ + n > s_.size()) fail("bad unicode escape");
          unsigned long cp = 0;
          for (size_t k = 0; k < n; ++k) {
            const char h = s_[i_ + k];
            if (!std::isxdigit(static_cast<unsigned char>(h))) fail("bad unicode escape");
            cp = cp * 16 + static_cast<unsigned long>(std::isdigit(static_cast<unsigned char>(h))
                                                          ? h - '0'
                                                          : (std::tolower(h) - 'a' + 10));
          }
          if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) fail("bad unicode escape");
          append_utf8(out, cp);
          i_ += n;
          break;
        }
        default: fail(std::string("unknown escape \\") + e);
      }
    }
  }

  std::string_view s_;
  size_t i_ = 0;
  int line_ = 1;
};

void warn(int line, const std::string& msg) {
  std::cerr << "ssg: warning: site.toml:" << line << ": " << msg << "\n";
}

}  // namespace

SiteConfig load_site_config(const std::filesystem::path& content_dir) {
  const auto toml_path = content_dir / "site.toml";
  if (!std::filesystem::exists(toml_path)) {
    throw std::runtime_error("missing site.toml in " + content_dir.string());
  }

  SiteConfig cfg;
  cfg.content_dir = std::filesystem::absolute(content_dir).lexically_normal();
  if (!cfg.content_dir.has_filename() && cfg.content_dir.has_relative_path()) {
    cfg.content_dir = cfg.content_dir.parent_path();  // "dir/" -> "dir"
  }

  std::string dist, templates, skeleton;
  std::string text = read_file(toml_path);
  if (starts_with(text, "\xEF\xBB\xBF")) text.erase(0, 3);  // UTF-8 BOM (Windows editors)
  Parser p(text);
  std::string table;
  std::set<std::string> seen;

  for (;;) {
    p.skip_space_lines();
    if (p.at_end()) break;
    const int line = p.line();
    if (p.peek() == '[') {
      table = p.table_header();
      if (!seen.insert("[" + table).second) p.fail("duplicate table [" + table + "]");
      if (table != "markdown") warn(line, "unknown table [" + table + "]");
      continue;
    }
    const std::string key = p.key();
    p.skip_space();
    p.expect('=', "= after '" + key + "'");
    p.skip_space();
    const Value v = p.value();
    p.end_of_line();
    auto err = [&](const std::string& msg) {
      throw std::runtime_error("site.toml:" + std::to_string(line) + ": " + msg);
    };
    auto want = [&](Value::Kind k) {
      if (v.kind != k) err("'" + key + "' must be " + kind_name(k));
    };
    if (!seen.insert(table + "." + key).second) err("duplicate key '" + key + "'");

    if (table.empty()) {
      if (key == "dist") { want(Value::String); dist = v.str; }
      else if (key == "templates") { want(Value::String); templates = v.str; }
      else if (key == "skeleton") { want(Value::String); skeleton = v.str; }
      else if (key == "base_url") { want(Value::String); cfg.base_url = v.str; }
      else if (key == "clean") { want(Value::Bool); cfg.clean = v.boolean; }
      else if (key == "nested_repos") { want(Value::Bool); cfg.nested_repos = v.boolean; }
      else if (key == "ignore") { want(Value::Array); cfg.ignore = v.array; }
      else warn(line, "unknown key '" + key + "'");
    } else if (table == "markdown") {
      auto& m = cfg.markdown;
      if (key == "html") { want(Value::Bool); m.raw_html = v.boolean; }
      else if (key == "scripts") { want(Value::Bool); m.scripts = v.boolean; }
      else if (key == "heading_ids") { want(Value::Bool); m.heading_ids = v.boolean; }
      else if (key == "autolinks") { want(Value::Bool); m.autolinks = v.boolean; }
      else if (key == "extensions") { want(Value::Bool); m.extensions = v.boolean; }
      else if (key == "text_width") {
        want(Value::Int);
        if (v.integer < 0 || v.integer > 100000) err("'text_width' must be between 0 and 100000");
        m.text_width = static_cast<int>(v.integer);
      } else {
        warn(line, "unknown key '" + key + "' in [markdown]");
      }
    }
    // Keys in unknown tables were already warned about via the table.
  }

  if (dist.empty() || templates.empty() || skeleton.empty()) {
    throw std::runtime_error("site.toml must set dist, templates, and skeleton");
  }
  while (!cfg.base_url.empty() && cfg.base_url.back() == '/') cfg.base_url.pop_back();

  auto resolve = [&](const std::string& path_str) {
    std::filesystem::path path(path_str);
    if (path.is_relative()) path = cfg.content_dir / path;
    path = path.lexically_normal();
    if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
    return path;
  };

  cfg.dist_dir = resolve(dist);
  cfg.templates_dir = resolve(templates);
  cfg.skeleton_path = resolve(skeleton);
  return cfg;
}
