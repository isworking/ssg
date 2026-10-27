#include "features.hpp"

#include "toml.hpp"
#include "util.hpp"

#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

constexpr int kMaxJsonDepth = 64;

bool key_part_ok(std::string_view k) {
  if (k.empty()) return false;
  for (char c : k) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
  }
  return true;
}

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

struct Flattener {
  std::map<std::string, std::string>& out;
  std::vector<std::string>& skipped;
  void put(const std::string& key, std::string value) { out[key] = std::move(value); }
};

// A small JSON reader that writes straight into the flattened map.
class JsonReader {
 public:
  JsonReader(std::string_view s, Flattener& f) : s_(s), f_(f) {}

  void parse(const std::string& prefix) {
    skip_ws();
    value(prefix, 0);
    skip_ws();
    if (i_ != s_.size()) fail("unexpected text after the value");
  }

 private:
  std::string_view s_;
  size_t i_ = 0;
  Flattener& f_;

  [[noreturn]] void fail(const std::string& msg) const {
    int line = 1;
    for (size_t k = 0; k < i_ && k < s_.size(); ++k) line += s_[k] == '\n';
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
  }
  void skip_ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
      ++i_;
    }
  }
  bool eat(char c) {
    if (i_ < s_.size() && s_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }
  unsigned hex4() {
    if (i_ + 4 > s_.size()) fail("bad \\u escape");
    unsigned v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++];
      if (!std::isxdigit(static_cast<unsigned char>(c))) fail("bad \\u escape");
      v = v * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c))
                                             ? c - '0'
                                             : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
    }
    return v;
  }
  std::string string() {
    if (!eat('"')) fail("expected a string");
    std::string out;
    for (;;) {
      if (i_ >= s_.size()) fail("unterminated string");
      const char c = s_[i_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) fail("raw control character in a string");
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) fail("unterminated string");
      switch (const char e = s_[i_++]) {
        case '"': case '\\': case '/': out += e; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00) {
            if (!(i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u')) {
              fail("unpaired surrogate");
            }
            i_ += 2;
            const unsigned lo = hex4();
            if (lo < 0xDC00 || lo > 0xDFFF) fail("unpaired surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            fail("unpaired surrogate");
          }
          append_utf8(out, cp);
          break;
        }
        default: fail(std::string("bad escape \\") + e);
      }
    }
  }
  bool word(std::string_view w) {
    if (s_.substr(i_, w.size()) != w) return false;
    i_ += w.size();
    return true;
  }
  void value(const std::string& key, int depth) {
    if (depth > kMaxJsonDepth) fail("nested more than 64 levels deep");
    skip_ws();
    if (i_ >= s_.size()) fail("unexpected end of input");
    const char c = s_[i_];
    if (c == '{') {
      ++i_;
      skip_ws();
      if (eat('}')) return;
      for (;;) {
        skip_ws();
        const std::string k = string();
        skip_ws();
        if (!eat(':')) fail("expected ':'");
        if (key_part_ok(k)) {
          value(key + "." + k, depth + 1);
        } else {
          f_.skipped.push_back(key + "." + k);
          std::map<std::string, std::string> scratch;
          std::vector<std::string> ignored;
          Flattener sink{scratch, ignored};
          JsonReader sub(s_, sink);
          sub.i_ = i_;
          sub.value("x", depth + 1);
          i_ = sub.i_;
        }
        skip_ws();
        if (eat(',')) continue;
        if (eat('}')) return;
        fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++i_;
      skip_ws();
      size_t n = 0;
      if (!eat(']')) {
        for (;;) {
          value(key + "." + std::to_string(n++), depth + 1);
          skip_ws();
          if (eat(',')) continue;
          if (eat(']')) break;
          fail("expected ',' or ']'");
        }
      }
      f_.put(key + ".length", std::to_string(n));
      return;
    }
    if (c == '"') {
      f_.put(key, string());
    } else if (word("true")) {
      f_.put(key, "true");
    } else if (word("false")) {
      f_.put(key, "false");
    } else if (word("null")) {
      f_.put(key, "");
    } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      const size_t start = i_;
      while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' ||
                                s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) {
        ++i_;
      }
      f_.put(key, std::string(s_.substr(start, i_ - start)));
    } else {
      fail("unexpected character");
    }
  }
};

void put_toml_value(Flattener& f, const std::string& key, const TomlValue& v) {
  switch (v.kind) {
    case TomlValue::String: f.put(key, v.str); break;
    case TomlValue::Bool: f.put(key, v.boolean ? "true" : "false"); break;
    case TomlValue::Int: f.put(key, std::to_string(v.integer)); break;
    case TomlValue::Array:
      for (size_t i = 0; i < v.array.size(); ++i) {
        put_toml_value(f, key + "." + std::to_string(i), v.array[i]);
      }
      f.put(key + ".length", std::to_string(v.array.size()));
      break;
  }
}

}  // namespace

void flatten_data(std::string_view text, bool is_json, const std::string& prefix,
                  std::map<std::string, std::string>& out, std::vector<std::string>& skipped) {
  Flattener f{out, skipped};
  if (is_json) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    JsonReader(text, f).parse(prefix);
    return;
  }
  if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
  const TomlDoc doc = parse_toml(text, "toml");
  for (const TomlEntry& e : doc.entries) {
    std::string key = prefix;
    bool ok = true;
    for (const std::string& part : e.path) {
      if (!key_part_ok(part)) ok = false;
      key += "." + part;
    }
    if (!ok) {
      skipped.push_back(key);
      continue;
    }
    put_toml_value(f, key, e.value);
  }
}

std::vector<Redirect> parse_redirects(std::string_view text, std::vector<Diag>& problems) {
  std::vector<Redirect> out;
  std::set<std::string> seen;
  int line_no = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(pos, nl - pos);
    pos = nl + 1;
    ++line_no;
    if (line_no == 1 && line.substr(0, 3) == "\xEF\xBB\xBF") line.remove_prefix(3);
    if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
    std::istringstream words{std::string(line)};
    std::vector<std::string> w;
    for (std::string t; words >> t;) w.push_back(t);
    if (w.empty()) continue;
    auto bad = [&](const std::string& msg) {
      problems.push_back({"redirects.txt", "line " + std::to_string(line_no) + ": " + msg});
    };
    if (w.size() < 2 || w.size() > 3) {
      bad("want 'from to [status]', skipped");
      continue;
    }
    Redirect r;
    r.from = w[0];
    r.to = w[1];
    r.line = line_no;
    if (r.from[0] != '/') {
      bad("'" + r.from + "' must start with /, skipped");
      continue;
    }
    if (r.to[0] != '/' && r.to.rfind("http://", 0) != 0 && r.to.rfind("https://", 0) != 0) {
      bad("'" + r.to + "' must be a path starting with / or an http(s) URL, skipped");
      continue;
    }
    if (w.size() == 3) {
      const std::string& st = w[2];
      if (st != "301" && st != "302" && st != "303" && st != "307" && st != "308") {
        bad("status '" + st + "' must be 301, 302, 303, 307 or 308, skipped");
        continue;
      }
      r.status = std::stoi(st);
    }
    if (!seen.insert(r.from).second) {
      bad("'" + r.from + "' is already redirected above, skipped");
      continue;
    }
    out.push_back(std::move(r));
  }
  return out;
}

std::string redirects_file(const std::vector<Redirect>& rs) {
  std::string o;
  for (const Redirect& r : rs) o += r.from + " " + r.to + " " + std::to_string(r.status) + "\n";
  return o;
}

std::string redirect_stub_path(const std::string& from) {
  if (from.empty() || from[0] != '/' || from.find_first_of("*:?") != std::string::npos) return "";
  std::string rel = from.substr(1);
  // No stub may land outside dist.
  for (size_t at = 0; at <= rel.size();) {
    size_t end = rel.find('/', at);
    if (end == std::string::npos) end = rel.size();
    const std::string part = rel.substr(at, end - at);
    if (part == ".." || part == ".") return "";
    at = end + 1;
  }
  if (rel.empty() || rel.back() == '/') return rel + "index.html";
  const size_t slash = rel.rfind('/');
  const std::string last = rel.substr(slash == std::string::npos ? 0 : slash + 1);
  // /old.html is a file of its own; /old is the directory /old/.
  return last.find('.') != std::string::npos ? rel : rel + "/index.html";
}

std::string redirect_stub_html(const Redirect& r, const std::string& base_url) {
  const std::string to = html_escape_attr(r.to);
  const std::string canonical = html_escape_attr(r.to[0] == '/' ? base_url + r.to : r.to);
  return "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
         "<title>Redirecting</title>\n<meta name=\"robots\" content=\"noindex\">\n" +
         std::string(base_url.empty() && r.to[0] == '/'
                         ? ""
                         : "<link rel=\"canonical\" href=\"" + canonical + "\">\n") +
         "<meta http-equiv=\"refresh\" content=\"0; url=" + to + "\">\n</head>\n<body>\n"
         "<p>Moved to <a href=\"" + to + "\">" + to + "</a>.</p>\n</body>\n</html>\n";
}

std::string robots_txt(const RobotsConfig& cfg, const std::string& sitemap_url) {
  std::string o = "User-agent: *\n";
  if (cfg.disallow.empty()) o += "Allow: /\n";
  for (const std::string& d : cfg.disallow) o += "Disallow: " + d + "\n";
  if (!sitemap_url.empty()) o += "\nSitemap: " + sitemap_url + "\n";
  return o;
}

std::string feed_xml(const std::string& title, const std::string& author,
                     const std::string& site_url, const std::string& self_url,
                     const std::string& updated, const std::vector<FeedEntry>& entries) {
  auto esc = [](std::string_view s) { return html_escape_attr(s); };
  std::string o =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <title>" + esc(title) + "</title>\n"
      "  <link rel=\"alternate\" href=\"" + esc(site_url) + "\"/>\n"
      "  <link rel=\"self\" href=\"" + esc(self_url) + "\"/>\n"
      "  <id>" + esc(self_url) + "</id>\n"
      "  <updated>" + esc(updated) + "T00:00:00Z</updated>\n"
      "  <author><name>" + esc(author.empty() ? title : author) + "</name></author>\n";
  for (const FeedEntry& e : entries) {
    o += "  <entry>\n    <title>" + esc(e.title) + "</title>\n    <link href=\"" + esc(e.url) +
         "\"/>\n    <id>" + esc(e.url) + "</id>\n    <updated>" + esc(e.date.substr(0, 10)) +
         "T00:00:00Z</updated>\n";
    if (!e.author.empty() && e.author != author) {
      o += "    <author><name>" + esc(e.author) + "</name></author>\n";
    }
    if (!e.summary.empty()) o += "    <summary>" + esc(e.summary) + "</summary>\n";
    o += "  </entry>\n";
  }
  return o + "</feed>\n";
}

std::string slugify(std::string_view s) {
  std::string out;
  bool gap = false;
  for (const char ch : s) {
    const auto u = static_cast<unsigned char>(ch);
    if (std::isalnum(u) || u >= 0x80) {
      if (gap && !out.empty()) out += '-';
      gap = false;
      out += static_cast<char>(std::tolower(u));
    } else {
      gap = true;
    }
  }
  return out;
}
