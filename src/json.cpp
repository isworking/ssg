#include "json.hpp"

#include "util.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {

constexpr int kMaxDepth = 64;

void append_utf8(std::string& out, unsigned cp) {
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

class Reader {
 public:
  explicit Reader(std::string_view s) : s_(s) {}

  Json document() {
    Json v = value(0);
    ws();
    if (i_ != s_.size()) fail("unexpected text after the value");
    return v;
  }

 private:
  std::string_view s_;
  size_t i_ = 0;

  [[noreturn]] void fail(const std::string& msg) const {
    throw std::runtime_error("invalid JSON at byte " + std::to_string(i_) + ": " + msg);
  }
  void ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
  }
  bool eat(char c) {
    if (i_ < s_.size() && s_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }
  bool word(std::string_view w) {
    if (s_.substr(i_, w.size()) != w) return false;
    i_ += w.size();
    return true;
  }

  unsigned hex4() {
    if (i_ + 4 > s_.size()) fail("short \\u escape");
    unsigned v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else fail("bad \\u escape");
    }
    return v;
  }

  std::string string() {
    std::string out;
    while (true) {
      if (i_ >= s_.size()) fail("unterminated string");
      const unsigned char c = static_cast<unsigned char>(s_[i_++]);
      if (c == '"') return out;
      if (c < 0x20) fail("control character in string");
      if (c != '\\') {
        out += static_cast<char>(c);
        continue;
      }
      if (i_ >= s_.size()) fail("unterminated string");
      switch (s_[i_++]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && s_.substr(i_, 2) == "\\u") {
            const size_t save = i_;
            i_ += 2;
            const unsigned lo = hex4();
            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            else i_ = save;
          }
          // A lone surrogate becomes U+FFFD rather than invalid UTF-8.
          if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
          append_utf8(out, cp);
          break;
        }
        default: fail("bad escape");
      }
    }
  }

  Json number() {
    const size_t start = i_;
    eat('-');
    if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) fail("bad number");
    if (s_[i_] == '0') ++i_;  // no leading zeros
    else while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    bool real = false;
    if (eat('.')) {
      real = true;
      if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) fail("bad number");
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      real = true;
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) fail("bad number");
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    const std::string text(s_.substr(start, i_ - start));
    Json j;
    if (!real && text.size() < 18) {
      j.kind = Json::Int;
      j.integer = std::strtoll(text.c_str(), nullptr, 10);
    } else {
      j.kind = Json::Real;
      j.real = std::strtod(text.c_str(), nullptr);
    }
    return j;
  }

  Json value(int depth) {
    if (depth > kMaxDepth) fail("nested more than 64 levels deep");
    ws();
    if (i_ >= s_.size()) fail("unexpected end");
    const char c = s_[i_];
    if (c == '"') {
      ++i_;
      return Json(string());
    }
    if (c == '{') {
      ++i_;
      Json o = Json::make_object();
      ws();
      if (eat('}')) return o;
      while (true) {
        ws();
        if (!eat('"')) fail("object key must be a string");
        std::string k = string();
        ws();
        if (!eat(':')) fail("expected ':'");
        o.object[std::move(k)] = value(depth + 1);
        ws();
        if (eat(',')) continue;
        if (eat('}')) return o;
        fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++i_;
      Json a = Json::make_array();
      ws();
      if (eat(']')) return a;
      while (true) {
        a.array.push_back(value(depth + 1));
        ws();
        if (eat(',')) continue;
        if (eat(']')) return a;
        fail("expected ',' or ']'");
      }
    }
    if (word("true")) return Json(true);
    if (word("false")) return Json(false);
    if (word("null")) return Json();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number();
    fail("unexpected character");
  }
};

}  // namespace

const Json* Json::find(const std::string& key) const {
  if (kind != Object) return nullptr;
  const auto it = object.find(key);
  return it == object.end() ? nullptr : &it->second;
}

std::string Json::str_at(const std::string& key, const std::string& def) const {
  const Json* v = find(key);
  return v && v->kind == String ? v->str : def;
}

std::string Json::dump() const {
  switch (kind) {
    case Null: return "null";
    case Bool: return boolean ? "true" : "false";
    case Int: return std::to_string(integer);
    case Real: {
      // JSON has no inf or nan; and a real must keep a '.' or exponent so it reads back as one
      // (-0.0 would otherwise print as "-0" and come back as the integer 0).
      if (!std::isfinite(real)) return "null";
      char buf[40];
      std::snprintf(buf, sizeof buf, "%.17g", real);
      std::string s = buf;
      if (s.find_first_of(".e") == std::string::npos) s += ".0";
      return s;
    }
    case String: return json_str(str);
    case Array: {
      std::string o = "[";
      for (size_t i = 0; i < array.size(); ++i) o += (i ? "," : "") + array[i].dump();
      return o + "]";
    }
    case Object: {
      std::string o = "{";
      bool first = true;
      for (const auto& kv : object) {
        o += (first ? "" : ",") + json_str(kv.first) + ":" + kv.second.dump();
        first = false;
      }
      return o + "}";
    }
  }
  return "null";
}

Json parse_json(std::string_view text) { return Reader(text).document(); }
