#include "document.hpp"
#include "util.hpp"

#include <cctype>
#include <set>
#include <stdexcept>

static bool is_key_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
}

std::vector<Segment> parse_segments(std::string_view s) {
  std::vector<Segment> out;
  std::string literal;

  auto flush = [&]() {
    if (!literal.empty()) {
      out.push_back({false, std::move(literal)});
      literal.clear();
    }
  };

  for (size_t i = 0; i < s.size();) {
    if (!(s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{')) {
      literal.push_back(s[i]);
      ++i;
      continue;
    }

    size_t j = i + 2;
    while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;

    if (j + 6 <= s.size() && s.substr(j, 6) == "extend") {
      size_t k = j + 6;
      while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
      if (k < s.size() && s[k] == '"') {
        ++k;
        const size_t path_start = k;
        while (k < s.size() && s[k] != '"' && s[k] != '\n') ++k;
        if (k < s.size() && s[k] == '"') {
          const std::string path(s.substr(path_start, k - path_start));
          ++k;
          while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
          if (k + 1 < s.size() && s[k] == '}' && s[k + 1] == '}') {
            flush();
            out.push_back({true, "extend:" + path});
            i = k + 2;
            continue;
          }
        }
      }
    } else if (j < s.size() && is_key_char(s[j])) {
      const size_t key_start = j;
      while (j < s.size() && is_key_char(s[j])) ++j;
      const size_t key_end = j;
      while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
      if (j + 1 < s.size() && s[j] == '}' && s[j + 1] == '}') {
        flush();
        out.push_back({true, std::string(s.substr(key_start, key_end - key_start))});
        i = j + 2;
        continue;
      }
    }

    literal.push_back(s[i]);
    ++i;
  }
  flush();
  return out;
}

static bool parse_frontmatter_line(std::string_view line, std::string& key, std::string& value) {
  line = trim(line);
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos) return false;
  const auto k = trim(line.substr(0, colon));
  if (k.empty()) return false;
  for (char c : k) {
    if (!is_key_char(c)) return false;
  }
  auto rest = trim(line.substr(colon + 1));
  if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"') return false;
  key = std::string(k);
  value = std::string(rest.substr(1, rest.size() - 2));
  return true;
}

static bool take_line(std::string_view& rest, std::string_view& line) {
  if (rest.empty()) return false;
  const size_t nl = rest.find('\n');
  line = nl == std::string_view::npos ? rest : rest.substr(0, nl);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
  return true;
}

static bool is_fence(std::string_view line) { return trim(line) == "---"; }

ParsedDoc parse_markdown_source(std::string_view source) {
  ParsedDoc doc;
  std::string_view rest = source;
  std::string_view first;
  std::string_view peek = rest;
  const bool fenced = take_line(peek, first) && is_fence(first);

  if (fenced) {
    take_line(rest, first);
    bool closed = false;
    std::string_view line;
    while (take_line(rest, line)) {
      if (is_fence(line)) {
        closed = true;
        break;
      }
      if (trim(line).empty()) continue;
      std::string key, value;
      if (!parse_frontmatter_line(line, key, value)) {
        throw std::runtime_error("frontmatter values must be key: \"value\"");
      }
      doc.vars[key] = std::move(value);
    }
    if (!closed) throw std::runtime_error("unclosed frontmatter (missing closing ---)");
  } else {
    while (!rest.empty()) {
      std::string_view saved = rest;
      std::string_view line;
      take_line(rest, line);
      if (trim(line).empty()) break;
      std::string key, value;
      if (!parse_frontmatter_line(line, key, value)) {
        rest = saved;
        break;
      }
      doc.vars[key] = std::move(value);
    }
  }

  const auto body_view = ltrim(rest);
  const auto segs = parse_segments(body_view);
  if (!segs.empty() && segs[0].is_key && starts_with(segs[0].text, "extend:")) {
    doc.extend_file = segs[0].text.substr(7);
    const size_t close = body_view.find("}}");
    std::string_view after =
        close == std::string_view::npos ? std::string_view{} : body_view.substr(close + 2);
    if (!after.empty() && after.front() == '\r') after.remove_prefix(1);
    if (!after.empty() && after.front() == '\n') after.remove_prefix(1);
    doc.body = std::string(after);
  } else {
    doc.body = std::string(rest);
  }
  return doc;
}

static std::string interpolate_impl(std::string_view s, const std::map<std::string, std::string>& vars,
                                    std::set<std::string>& stack) {
  std::string out;
  for (const auto& seg : parse_segments(s)) {
    if (!seg.is_key) {
      out += seg.text;
      continue;
    }
    if (starts_with(seg.text, "extend:")) {
      out += "{{extend \"" + seg.text.substr(7) + "\"}}";
      continue;
    }
    auto it = vars.find(seg.text);
    if (it == vars.end() || stack.count(seg.text)) {
      out += "{{" + seg.text + "}}";
      continue;
    }
    stack.insert(seg.text);
    out += interpolate_impl(it->second, vars, stack);
    stack.erase(seg.text);
  }
  return out;
}

std::string interpolate(std::string_view s, const std::map<std::string, std::string>& vars) {
  std::set<std::string> stack;
  return interpolate_impl(s, vars, stack);
}

static std::filesystem::path template_path(const SiteConfig& cfg, const std::string& file) {
  std::filesystem::path p(file);
  if (p.extension() != ".md") p += ".md";
  return cfg.templates_dir / p;
}

RenderedDoc render_extended(const SiteConfig& cfg, const std::filesystem::path& source_path,
                            const ParsedDoc& doc) {
  std::map<std::string, std::string> vars = doc.vars;
  std::string body = doc.body;
  std::string extend = doc.extend_file;
  std::set<std::string> seen;

  while (!extend.empty()) {
    if (!seen.insert(extend).second) {
      throw std::runtime_error("cyclic {{extend}} in " + source_path.string());
    }
    const auto path = template_path(cfg, extend);
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error("missing template \"" + extend + "\" (looked for " + path.string() +
                               ")");
    }
    ParsedDoc tmpl = parse_markdown_source(read_file(path));
    for (const auto& kv : tmpl.vars) {
      if (!vars.count(kv.first)) vars[kv.first] = kv.second;
    }
    vars["content"] = body;
    body = interpolate(tmpl.body, vars);
    extend = tmpl.extend_file;
  }

  RenderedDoc out;
  for (const auto& kv : vars) out.vars[kv.first] = interpolate(kv.second, vars);
  out.markdown = interpolate(body, out.vars);
  return out;
}
