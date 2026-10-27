#include "toml.hpp"

#include <cctype>
#include <climits>
#include <map>
#include <stdexcept>

namespace {

bool is_bare_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

void append_utf8(std::string& out, unsigned long cp) {
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

// Arrays nest at most this deep; value() recurses once per level.
constexpr int kMaxArrayDepth = 64;

// How a path got defined, for TOML's "define each table once" rules.
enum class Def {
  Value,     // key = value
  Header,    // [a.b]
  Implicit,  // `a` in [a.b], until [a] itself appears
  Dotted,    // `a` in a.b = 1
};

class Parser {
 public:
  Parser(std::string_view s, const std::string& name) : s_(s), name_(name) {}

  TomlDoc parse() {
    TomlDoc doc;
    std::vector<std::string> table;
    for (;;) {
      skip_space_lines();
      if (at_end()) break;
      const int line = line_;
      if (peek() == '[') {
        table = table_header();
        define_header(table, line);
        doc.tables.push_back({table, line});
        continue;
      }
      std::vector<std::string> path = table;
      const std::vector<std::string> key = key_path();
      path.insert(path.end(), key.begin(), key.end());
      skip_space();
      expect('=', "= after '" + toml_path_string(key) + "'");
      skip_space();
      TomlValue v = value();
      end_of_line();
      define_key(path, table.size(), line);
      doc.entries.push_back({std::move(path), std::move(v), line});
    }
    return doc;
  }

 private:
  [[noreturn]] void fail(const std::string& msg) const { fail_at(line_, msg); }
  [[noreturn]] void fail_at(int line, const std::string& msg) const {
    throw std::runtime_error(name_ + ":" + std::to_string(line) + ": " + msg);
  }

  bool at_end() const { return i_ >= s_.size(); }
  char peek() const { return at_end() ? '\0' : s_[i_]; }

  void expect(char c, const std::string& what) {
    if (peek() != c) fail("expected " + what);
    ++i_;
  }

  void skip_space() {
    while (!at_end() && (s_[i_] == ' ' || s_[i_] == '\t')) ++i_;
  }

  // Up to the end of the line; a CR there is left for the caller. Like strings, comments may
  // not hold raw control characters other than tab.
  void skip_comment() {
    if (peek() != '#') return;
    for (; !at_end() && s_[i_] != '\n'; ++i_) {
      const char c = s_[i_];
      if (c == '\r' && peek_at(i_ + 1) == '\n') return;
      if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7F) {
        fail("control characters are not allowed in comments");
      }
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

  std::string key_part() {
    if (peek() == '"' || peek() == '\'') return string_value();
    const size_t start = i_;
    while (!at_end() && is_bare_char(s_[i_])) ++i_;
    if (i_ == start) fail("expected a key");
    return std::string(s_.substr(start, i_ - start));
  }

  // a, "a b", a.b."c.d" -- whitespace around the dots is allowed.
  std::vector<std::string> key_path() {
    std::vector<std::string> path{key_part()};
    for (;;) {
      skip_space();
      if (peek() != '.') return path;
      ++i_;
      skip_space();
      path.push_back(key_part());
    }
  }

  std::vector<std::string> table_header() {
    ++i_;  // [
    if (peek() == '[') fail("arrays of tables are not supported");
    skip_space();
    std::vector<std::string> path = key_path();
    if (peek() != ']') fail("expected ] after table name");
    ++i_;
    end_of_line();
    return path;
  }

  // [path]: every prefix becomes (at least) an implicit table; path itself must be new.
  void define_header(const std::vector<std::string>& path, int line) {
    std::vector<std::string> prefix;
    for (size_t k = 0; k + 1 < path.size(); ++k) {
      prefix.push_back(path[k]);
      auto it = defs_.find(prefix);
      if (it == defs_.end()) defs_.emplace(prefix, Def::Implicit);
      else if (it->second == Def::Value) fail_at(line, "'" + toml_path_string(prefix) + "' is not a table");
    }
    const std::string name = "[" + toml_path_string(path) + "]";
    auto it = defs_.find(path);
    if (it == defs_.end()) {
      defs_.emplace(path, Def::Header);
    } else if (it->second == Def::Implicit) {
      it->second = Def::Header;
    } else if (it->second == Def::Header) {
      fail_at(line, "duplicate table " + name);
    } else if (it->second == Def::Dotted) {
      fail_at(line, "table " + name + " was already defined by dotted keys");
    } else {
      fail_at(line, "table " + name + " was already defined as a value");
    }
  }

  // key = value under the current table (path[0, table_len)); dotted-key prefixes
  // past the table become dotted tables, which only dotted keys may extend.
  void define_key(const std::vector<std::string>& path, size_t table_len, int line) {
    std::vector<std::string> prefix(path.begin(), path.begin() + table_len);
    for (size_t k = table_len; k + 1 < path.size(); ++k) {
      prefix.push_back(path[k]);
      auto it = defs_.find(prefix);
      if (it == defs_.end()) {
        defs_.emplace(prefix, Def::Dotted);
      } else if (it->second == Def::Value) {
        fail_at(line, "'" + toml_path_string(prefix) + "' is not a table");
      } else if (it->second != Def::Dotted) {
        // An implicit table (`a.b` from [a.b.c]) has no header yet to add the key under.
        const std::string name = "[" + toml_path_string(prefix) + "]";
        fail_at(line, "table " + name + " can't be extended with dotted keys; " +
                          (it->second == Def::Header ? "add the key under its header"
                                                     : "give it a " + name + " header"));
      }
    }
    auto it = defs_.find(path);
    if (it == defs_.end()) {
      defs_.emplace(path, Def::Value);
    } else if (it->second == Def::Value) {
      fail_at(line, "duplicate key '" + toml_path_string(path) + "'");
    } else {
      fail_at(line, "'" + toml_path_string(path) + "' is already a table");
    }
  }

  TomlValue value() {
    TomlValue v;
    const char c = peek();
    if (c == '"' || c == '\'') {
      v.str = string_value();
    } else if (c == '[') {
      v.kind = TomlValue::Array;
      if (++depth_ > kMaxArrayDepth) {
        fail("arrays nest too deeply (more than " + std::to_string(kMaxArrayDepth) + " levels)");
      }
      ++i_;
      for (;;) {
        skip_space_lines();
        if (peek() == ']') break;
        v.array.push_back(value());
        skip_space_lines();
        if (peek() == ',') {
          ++i_;
          continue;
        }
        if (peek() != ']') fail("expected , or ] in array");
      }
      ++i_;
      --depth_;
    } else if (word("true")) {
      v.kind = TomlValue::Bool;
      v.boolean = true;
    } else if (word("false")) {
      v.kind = TomlValue::Bool;
    } else if (c == '+' || c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      v.kind = TomlValue::Int;
      v.integer = integer();
    } else if (c == '{') {
      fail("inline tables are not supported");
    } else if (word("inf") || word("nan")) {
      fail("only integers are supported");
    } else {
      fail("expected a value");
    }
    return v;
  }

  bool word(std::string_view w) {
    if (s_.substr(i_, w.size()) != w) return false;
    if (i_ + w.size() < s_.size() && is_bare_char(s_[i_ + w.size()])) return false;
    i_ += w.size();
    return true;
  }

  // Decimal only: no leading zeros, and each _ must sit between two digits.
  long long integer() {
    const bool neg = peek() == '-';
    if (peek() == '+' || peek() == '-') ++i_;
    if (word("inf") || word("nan")) fail("only integers are supported");
    auto digit = [&](size_t at) {
      return at < s_.size() && std::isdigit(static_cast<unsigned char>(s_[at]));
    };
    if (!digit(i_)) fail("expected a number");
    if (s_[i_] == '0' && i_ + 1 < s_.size() && (s_[i_ + 1] == 'x' || s_[i_ + 1] == 'o' ||
                                                 s_[i_ + 1] == 'b')) {
      fail("only decimal integers are supported");
    }
    if (s_[i_] == '0' && (digit(i_ + 1) || peek_at(i_ + 1) == '_')) {
      fail("leading zeros are not allowed");
    }
    long long n = 0;
    while (digit(i_) || peek_at(i_) == '_') {
      if (s_[i_] == '_') {
        if (!digit(i_ - 1) || !digit(i_ + 1)) fail("'_' must be between digits");
      } else {
        // Accumulate with the final sign so LLONG_MIN, which has no positive twin, fits.
        const int d = s_[i_] - '0';
        if (neg ? n < (LLONG_MIN + d) / 10 : n > (LLONG_MAX - d) / 10) {
          fail("integer out of range");
        }
        n = neg ? n * 10 - d : n * 10 + d;
      }
      ++i_;
    }
    const char next = peek();
    if (next == '.' || next == 'e' || next == 'E') fail("only integers are supported");
    if (next == ':' || next == '-') fail("dates and times are not supported");
    return n;
  }

  char peek_at(size_t at) const { return at < s_.size() ? s_[at] : '\0'; }

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
      if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7F) {
        fail("control characters in strings must be escaped");
      }
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
  const std::string& name_;
  size_t i_ = 0;
  int line_ = 1;
  int depth_ = 0;  // arrays currently open
  std::map<std::vector<std::string>, Def> defs_;
};

}  // namespace

TomlDoc parse_toml(std::string_view text, const std::string& name) {
  return Parser(text, name).parse();
}

const char* toml_kind_name(TomlValue::Kind k) {
  switch (k) {
    case TomlValue::String: return "a string";
    case TomlValue::Bool: return "a boolean";
    case TomlValue::Int: return "an integer";
    case TomlValue::Array: return "an array";
  }
  return "?";
}

std::string toml_path_string(const std::vector<std::string>& path) {
  std::string out;
  for (const std::string& part : path) {
    if (!out.empty()) out += '.';
    bool bare = !part.empty();
    for (char c : part) bare = bare && is_bare_char(c);
    if (bare) {
      out += part;
      continue;
    }
    out += '"';
    for (char c : part) {
      if (c == '"' || c == '\\') {
        out += '\\';
        out += c;
      } else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
        static const char hex[] = "0123456789ABCDEF";
        out += "\\u00";
        out += hex[(c >> 4) & 0xF];
        out += hex[c & 0xF];
      } else {
        out += c;
      }
    }
    out += '"';
  }
  return out;
}
