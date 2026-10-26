#include "build.hpp"
#include "document.hpp"
#include "ignore.hpp"
#include "markdown.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Private-use codepoints around a word: can't come from a source file by accident.
const std::string kTocSentinel = "\xEE\x80\x80toc\xEE\x80\x81";
const std::string kMainSentinel = "\xEE\x80\x80main\xEE\x80\x81";
const std::string kTitleSentinel = "\xEE\x80\x80title\xEE\x80\x81";
// Written into dist by every build; clean only empties a dist that has it.
const char* kDistMarker = ".ssg-dist";

using Vars = std::map<std::string, std::string>;

struct Page {
  fs::path src;
  std::string rel;      // source, relative to the content dir: "a/b.md"
  std::string out_rel;  // published markdown, relative to dist: "a/b/index.md"
  std::string url;      // "/a/b/"
  ParsedDoc doc;
  Vars vars;  // pass 1
  size_t words = 0;
  int parent = -1;
  std::vector<int> children;  // sorted
  bool has_headings = false;  // any ##+ heading in pass 1, i.e. a non-empty toc
};

struct Ctx {
  const SiteConfig& cfg;
  const BuildOptions& opt;
  IgnoreRules rules;
  std::vector<std::string> md_rels;
  std::vector<std::string> asset_rels;
  std::map<std::string, std::string> outputs;  // dist-relative path -> source that wrote it
  size_t warnings = 0;

  void warn(const std::string& msg) {
    std::cerr << "ssg: warning: " << msg << "\n";
    ++warnings;
  }

  void claim(const std::string& out, const std::string& src) {
    auto it = outputs.find(out);
    if (it == outputs.end()) {
      outputs.emplace(out, src);
      return;
    }
    warn(it->second + " and " + src + " both write " + out + " (the latter wins)");
    it->second = src;
  }
};

bool path_is_inside(const fs::path& dir, const fs::path& p) {
  const auto d = dir.lexically_normal();
  const auto n = p.lexically_normal();
  auto dit = d.begin();
  auto nit = n.begin();
  for (; dit != d.end() && nit != n.end(); ++dit, ++nit) {
    if (*dit == ".") continue;
    if (*nit != *dit) return false;
  }
  return dit == d.end();
}

bool is_markdown(std::string_view rel) { return ends_with(rel, ".md"); }

std::string parent_dir(std::string_view rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string_view::npos ? std::string() : std::string(rel.substr(0, slash));
}

std::string base_name(std::string_view rel) {
  const size_t slash = rel.rfind('/');
  return std::string(slash == std::string_view::npos ? rel : rel.substr(slash + 1));
}

std::string join_rel(const std::string& dir, std::string_view name) {
  return dir.empty() ? std::string(name) : dir + "/" + std::string(name);
}

// foo.md is published as foo/index.md unless foo/index.md exists.
std::string publish_rel(const std::string& rel, const std::set<std::string>& md_rels) {
  if (base_name(rel) == "index.md") return rel;
  const std::string stem = base_name(rel).substr(0, base_name(rel).size() - 3);
  const std::string as_index = join_rel(parent_dir(rel), stem + "/index.md");
  return md_rels.count(as_index) ? rel : as_index;
}

std::string url_encode_path(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
      out += c;
    } else {
      out += '%';
      out += hex[u >> 4];
      out += hex[u & 15];
    }
  }
  return out;
}

std::string url_for(const std::string& out_rel) {
  if (out_rel == "index.md") return "/";
  if (ends_with(out_rel, "/index.md")) {
    return "/" + url_encode_path(out_rel.substr(0, out_rel.size() - 8));
  }
  return "/" + url_encode_path(out_rel.substr(0, out_rel.size() - 3));
}

// Latest time %Y-%m-%d can print as four digits: 9999-12-31T23:59:59Z.
constexpr long long kMaxEpoch = 253402300799LL;

std::string build_date(Ctx& c) {
  std::time_t t = std::time(nullptr);
  // SOURCE_DATE_EPOCH makes builds reproducible (and the tests stable).
  if (const char* sde = std::getenv("SOURCE_DATE_EPOCH")) {
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(sde, &end, 10);
    if (end != sde && *end == '\0' && errno == 0 && v >= 0 && v <= kMaxEpoch) {
      t = static_cast<std::time_t>(v);
    } else {
      c.warn("ignoring SOURCE_DATE_EPOCH=" + std::string(sde) +
             " (want seconds from 0 to " + std::to_string(kMaxEpoch) + "); using the current time");
    }
  }
  std::tm tm{};
#ifdef _WIN32
  const bool ok = gmtime_s(&tm, &t) == 0;
#else
  const bool ok = gmtime_r(&t, &tm) != nullptr;
#endif
  char buf[32];
  if (!ok || std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm) == 0) {
    throw std::runtime_error("cannot format the build date");
  }
  return buf;
}

size_t count_words(std::string_view text) {
  size_t n = 0;
  bool in_word = false, has_alnum = false;
  for (char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isspace(u)) {
      if (in_word && has_alnum) ++n;
      in_word = has_alnum = false;
      continue;
    }
    in_word = true;
    if (std::isalnum(u) || u >= 0x80) has_alnum = true;
  }
  if (in_word && has_alnum) ++n;
  return n;
}

// Plain text that markdown would otherwise read as markup.
std::string md_escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']' || c == '<' ||
        c == '>' || c == '&' || c == '~' || c == '=' || c == '|') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

std::string var_or(const Vars& v, const std::string& k) {
  auto it = v.find(k);
  return it == v.end() ? std::string() : it->second;
}

// Template text that stands for itself: a later evaluation turns \{{ back into {{.
std::string lit(std::string_view s) { return replace_all(s, "{{", "\\{{"); }

std::string nav_title(const Vars& vars, const std::string& rel) {
  for (const char* k : {"nav_title", "page_title", "title"}) {
    const auto v = var_or(vars, k);
    if (!trim(v).empty()) return v;
  }
  if (base_name(rel) == "index.md") {
    const auto d = parent_dir(rel);
    return d.empty() ? "index" : base_name(d);
  }
  return base_name(rel).substr(0, base_name(rel).size() - 3);
}

// Anything but a number (garbage, nan) counts as no order; nan would break the sort.
bool parse_order(const Vars& v, double& out) {
  const auto s = std::string(trim(var_or(v, "order")));
  if (s.empty()) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end && *end == '\0' && !std::isnan(out);
}

bool page_before(const Page& a, const Page& b) {
  double oa = 0, ob = 0;
  const bool ha = parse_order(a.vars, oa), hb = parse_order(b.vars, ob);
  if (ha != hb) return ha;
  if (ha && oa != ob) return oa < ob;
  const auto da = var_or(a.vars, "date"), db = var_or(b.vars, "date");
  if (da.empty() != db.empty()) return !da.empty();
  if (da != db) return da < db;
  return a.rel < b.rel;
}

std::string toc_markdown(const std::vector<Heading>& hs) {
  int base = 7;
  for (const auto& h : hs) {
    if (h.level >= 2) base = std::min(base, h.level);
  }
  std::string out;
  int prev = -1;
  for (const auto& h : hs) {
    if (h.level < 2) continue;
    const int depth = std::min(h.level - base, prev + 1);
    prev = depth;
    out.append(static_cast<size_t>(depth) * 2, ' ');
    out += "- ";
    if (h.id.empty()) out += md_escape(h.text);
    else out += "[" + md_escape(h.text) + "](#" + h.id + ")";
    out += '\n';
  }
  return out;
}

// Swap each toc sentinel for the list, on lines of its own with blank lines around.
std::string expand_toc(const std::string& md, const std::string& toc) {
  if (toc.empty()) return replace_all(md, kTocSentinel, "");
  std::vector<std::string> out;
  bool blank_after = false;
  auto push = [&](std::string line) {
    if (blank_after && !trim(line).empty()) out.emplace_back();
    blank_after = false;
    out.push_back(std::move(line));
  };
  size_t ls = 0;
  for (;;) {
    const size_t le = md.find('\n', ls);
    const std::string line = md.substr(ls, le == std::string::npos ? std::string::npos : le - ls);
    if (line.find(kTocSentinel) == std::string::npos) {
      push(line);
    } else {
      for (size_t from = 0;;) {
        const size_t at = line.find(kTocSentinel, from);
        const auto piece = std::string_view(line).substr(
            from, at == std::string::npos ? std::string::npos : at - from);
        if (!trim(piece).empty()) push(std::string(from == 0 ? rtrim(piece) : trim(piece)));
        if (at == std::string::npos) break;
        if (!out.empty() && !trim(out.back()).empty()) out.emplace_back();
        for (size_t ts = 0; ts < toc.size();) {
          const size_t te = toc.find('\n', ts);
          out.push_back(toc.substr(ts, te - ts));
          ts = te + 1;
        }
        blank_after = true;
        from = at + kTocSentinel.size();
      }
    }
    if (le == std::string::npos) break;
    ls = le + 1;
  }
  std::string joined;
  for (size_t i = 0; i < out.size(); ++i) {
    if (i) joined += '\n';
    joined += out[i];
  }
  return joined;
}

struct Skeleton {
  std::vector<Segment> with_title;     // <title> inner replaced by kTitleSentinel
  std::vector<Segment> without_title;  // for pages without a title var
  bool uses_toc = false;
};

// Same structural errors as templates report.
void check_conditionals(const std::vector<Segment>& segs) {
  std::vector<std::string> open;  // the {{if}} tags, as written
  std::vector<bool> had_else;
  for (const auto& seg : segs) {
    if (!seg.is_key) continue;
    const std::string& t = seg.text;
    if (starts_with(t, "if:") || starts_with(t, "if-not:")) {
      const bool negate = starts_with(t, "if-not:");
      open.push_back("{{if " + std::string(negate ? "not " : "") + t.substr(negate ? 7 : 3) + "}}");
      had_else.push_back(false);
    } else if (t == "else:" || t == "end:") {
      if (open.empty()) throw std::runtime_error("{{" + t.substr(0, t.size() - 1) + "}} without {{if}}");
      if (t == "end:") {
        open.pop_back();
        had_else.pop_back();
      } else if (had_else.back()) {
        throw std::runtime_error("second {{else}} in one {{if}}");
      } else {
        had_else.back() = true;
      }
    }
  }
  if (!open.empty()) throw std::runtime_error("missing {{end}} for " + open.back());
}

Skeleton load_skeleton(const SiteConfig& cfg) {
  std::string src = read_file(cfg.skeleton_path);
  bool has_content = false;
  const auto segs = parse_segments(src);
  check_conditionals(segs);
  for (const auto& seg : segs) {
    if (seg.is_key && seg.text == "content") has_content = true;
  }
  if (!has_content) {
    src = replace_ci_tag_inner(src, "<main", "</main>", "\n" + kMainSentinel, true, false);
  }
  Skeleton sk;
  sk.without_title = parse_segments(src);
  sk.with_title = parse_segments(
      replace_ci_tag_inner(src, "<title", "</title>", kTitleSentinel, false, false));
  for (const auto& seg : sk.without_title) {
    if (seg.is_key && seg.text == "toc") sk.uses_toc = true;
  }
  return sk;
}

// {{key}} is filled HTML-escaped, {{content}} and {{toc}} raw; {{if}}/{{else}}/{{end}} work
// as in templates; \{{ is a literal {{; anything unknown stays as written.
std::string render_skeleton(const Skeleton& sk, const Vars& vars, const std::string& body,
                            const std::string& toc_html) {
  // What parse_segments turns an escaped \{{ into.
  static const std::string escaped_open = parse_segments("\\{{").at(0).text;
  auto title = vars.find("title");
  const auto& segs = title != vars.end() ? sk.with_title : sk.without_title;
  const std::string title_html = title != vars.end() ? html_escape(title->second) : std::string();
  // "<main>\n" + body + "</main>", with the closing tag on its own line.
  const std::string main_html = body.empty() || body.back() == '\n' ? body : body + "\n";
  auto lookup = [&](const std::string& k, std::string& v) {
    if (k == "content") v = body;
    else if (k == "toc") v = toc_html;
    else if (auto it = vars.find(k); it != vars.end()) v = it->second;
    else return false;
    return true;
  };

  struct Cond {
    bool outer;  // enclosing branch is live
    bool cond;
  };
  std::vector<Cond> conds;
  bool live = true;
  std::string out;
  for (const auto& seg : segs) {
    const std::string& t = seg.text;
    if (seg.is_key && (starts_with(t, "if:") || starts_with(t, "if-not:"))) {
      const bool negate = starts_with(t, "if-not:");
      std::string v;
      const bool truthy = lookup(t.substr(negate ? 7 : 3), v) && is_truthy(v);
      conds.push_back({live, truthy != negate});
      live = live && conds.back().cond;
      continue;
    }
    if (seg.is_key && (t == "else:" || t == "end:")) {
      // load_skeleton checked the nesting; only <title>'s replaced inner text can unbalance it.
      if (conds.empty()) continue;
      if (t == "else:") {
        live = conds.back().outer && !conds.back().cond;
      } else {
        live = conds.back().outer;
        conds.pop_back();
      }
      continue;
    }
    if (!live) continue;
    if (!seg.is_key) {
      const std::string text = replace_all(t, escaped_open, "{{");
      out += replace_all(replace_all(text, kMainSentinel, main_html), kTitleSentinel, title_html);
      continue;
    }
    std::string v;
    if (t == "content" || t == "toc") {
      lookup(t, v);
      out += v;
    } else if (starts_with(t, "extend:") || starts_with(t, "include:")) {
      const size_t colon = t.find(':');
      out += "{{" + t.substr(0, colon) + " \"" + t.substr(colon + 1) + "\"}}";
    } else if (lookup(t, v)) {
      out += html_escape_attr(v);
    } else {
      out += "{{" + t + "}}";
    }
  }
  return out;
}

void walk(Ctx& c, const fs::path& dir, const std::string& rel_dir) {
  const auto ai = dir / ".assetignore";
  if (fs::is_regular_file(ai)) c.rules.add_file(rel_dir, read_file(ai));

  std::vector<fs::directory_entry> entries;
  for (const auto& e : fs::directory_iterator(dir)) entries.push_back(e);
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });

  for (const auto& e : entries) {
    const std::string name = e.path().filename().string();
    const bool is_dir = e.is_directory();
    // Hidden files and dirs never ship (.git, .github, .env, ...), except .well-known/.
    if (name.empty() || (name[0] == '.' && !(is_dir && name == ".well-known"))) continue;
    const auto p = e.path().lexically_normal();
    if (p == c.cfg.dist_dir || p == c.cfg.templates_dir || p == c.cfg.skeleton_path) continue;
    if (rel_dir.empty() && name == "site.toml") continue;

    const std::string rel = join_rel(rel_dir, name);
    if (c.rules.ignored(rel, is_dir)) continue;
    if (is_dir) {
      // A nested git checkout is another project, not content.
      if (!c.cfg.nested_repos && fs::exists(fs::symlink_status(p / ".git"))) {
        if (!c.opt.quiet) {
          std::cerr << "ssg: note: skipping " << rel
                    << "/ (a nested git checkout; set nested_repos = true to build it)\n";
        }
        continue;
      }
      if (!e.is_symlink()) walk(c, p, rel);
      continue;
    }
    if (!e.is_regular_file()) continue;
    (is_markdown(rel) ? c.md_rels : c.asset_rels).push_back(rel);
  }
}

// Refuse a dist that would overwrite or wipe sources.
void check_dist(const SiteConfig& cfg) {
  const auto content = fs::weakly_canonical(cfg.content_dir);
  const auto dist = fs::weakly_canonical(cfg.dist_dir);
  const std::string what = "dist (" + cfg.dist_dir.string() + ") ";
  if (path_is_inside(dist, content)) {
    throw std::runtime_error(what + "must not be the content dir or contain it");
  }
  if (path_is_inside(dist, fs::weakly_canonical(cfg.templates_dir))) {
    throw std::runtime_error(what + "must not be or contain the templates dir");
  }
  if (path_is_inside(dist, fs::weakly_canonical(cfg.skeleton_path))) {
    throw std::runtime_error(what + "must not contain the skeleton");
  }
  // The parts of dist below what it shares with the content dir: none may be hidden (.git, ...).
  auto d = dist.begin();
  for (auto ci = content.begin(); d != dist.end() && ci != content.end() && *d == *ci; ++d, ++ci) {
  }
  for (; d != dist.end(); ++d) {
    const std::string part = d->string();
    if (!part.empty() && part[0] == '.') {
      throw std::runtime_error(what + "must not be inside a hidden dir (" + part + ")");
    }
  }
}

// Whether dist is ssg's to empty: missing, empty, or holding kDistMarker from an earlier build.
// A mistyped dist (say, a dir of sources) is never emptied or marked.
bool own_dist(const fs::path& dist, std::string& why) {
  std::error_code ec;
  const auto st = fs::symlink_status(dist, ec);
  if (!fs::exists(st)) return true;
  if (fs::is_symlink(st) || !fs::is_directory(st)) {
    why = "not a plain directory";
    return false;
  }
  if (fs::is_empty(dist) || fs::exists(fs::symlink_status(dist / kDistMarker))) return true;
  why = std::string("it wasn't created by ssg (no ") + kDistMarker + "); delete it once by hand";
  return false;
}

// Ready `dest` (inside dist) for writing: symlinks on the way are removed, never followed.
void prepare_dest(const fs::path& dist, const fs::path& dest) {
  fs::path at = dist;
  for (const auto& part : dest.lexically_relative(dist)) {
    at /= part;
    if (fs::is_symlink(fs::symlink_status(at))) fs::remove(at);
  }
  fs::create_directories(dest.parent_path());
}

bool looks_like_date(std::string_view v) {
  if (v.size() < 10) return false;
  for (size_t i = 0; i < 10; ++i) {
    const bool dash = i == 4 || i == 7;
    if (dash ? v[i] != '-' : !std::isdigit(static_cast<unsigned char>(v[i]))) return false;
  }
  return v.size() == 10 || v[10] == 'T' || v[10] == ' ';
}

std::string html_unescape_basic(std::string_view s) {
  std::string out = replace_all(s, "&quot;", "\"");
  out = replace_all(out, "&#39;", "'");
  out = replace_all(out, "&lt;", "<");
  out = replace_all(out, "&gt;", ">");
  return replace_all(out, "&amp;", "&");
}

std::string percent_decode(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

size_t find_ci(std::string_view hay, std::string_view needle, size_t from) {
  for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
    if (iequals(hay.substr(i, needle.size()), needle)) return i;
  }
  return std::string_view::npos;
}

// href/src attribute values that should exist in dist; skips anything with a scheme or only a
// fragment. Only real tags count: escaped text (&lt;a href=...) and comments aren't scanned, nor
// is the inside of <script>/<style>.
std::vector<std::string> local_links(std::string_view html) {
  std::vector<std::string> out;
  auto is_space = [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; };
  auto add = [&](std::string v) {
    v = html_unescape_basic(trim(v));
    if (v.empty() || v[0] == '#' || starts_with(v, "//")) return;
    const size_t colon = v.find(':');
    if (colon != std::string::npos && colon < v.find_first_of("/?#")) return;
    v = v.substr(0, v.find_first_of("?#"));
    if (!v.empty()) out.push_back(percent_decode(v));
  };
  size_t i = 0;
  while ((i = html.find('<', i)) != std::string_view::npos) {
    if (html.substr(i, 4) == "<!--") {
      const size_t e = html.find("-->", i + 4);
      if (e == std::string_view::npos) break;
      i = e + 3;
      continue;
    }
    ++i;
    if (i >= html.size() || !std::isalpha(static_cast<unsigned char>(html[i]))) continue;
    size_t ne = i;
    while (ne < html.size() && !is_space(html[ne]) && html[ne] != '>' && html[ne] != '/') ++ne;
    const std::string_view tag = html.substr(i, ne - i);
    i = ne;
    // Attributes up to the closing '>'.
    while (i < html.size() && html[i] != '>') {
      if (is_space(html[i]) || html[i] == '/') {
        ++i;
        continue;
      }
      size_t an = i;
      while (an < html.size() && !is_space(html[an]) && html[an] != '=' && html[an] != '>' &&
             html[an] != '/') {
        ++an;
      }
      const std::string_view name = html.substr(i, an - i);
      i = an;
      while (i < html.size() && is_space(html[i])) ++i;
      if (i >= html.size() || html[i] != '=') continue;  // no value
      ++i;
      while (i < html.size() && is_space(html[i])) ++i;
      std::string_view value;
      if (i < html.size() && (html[i] == '"' || html[i] == '\'')) {
        const size_t e = html.find(html[i], i + 1);
        if (e == std::string_view::npos) return out;
        value = html.substr(i + 1, e - i - 1);
        i = e + 1;
      } else {
        const size_t vs = i;
        while (i < html.size() && !is_space(html[i]) && html[i] != '>') ++i;
        value = html.substr(vs, i - vs);
      }
      if (iequals(name, "href") || iequals(name, "src")) add(std::string(value));
    }
    if (iequals(tag, "script") || iequals(tag, "style")) {
      const size_t e = find_ci(html, "</" + std::string(tag), i);
      if (e == std::string_view::npos) break;
      i = e;
    }
  }
  return out;
}

// Resolve `link` against a page URL; false when it climbs above the site root.
bool resolve_link(const std::string& page_url, const std::string& link, std::string& out) {
  std::string full = link[0] == '/' ? link : page_url.substr(0, page_url.rfind('/') + 1) + link;
  const bool trailing = full.back() == '/';
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= full.size()) {
    size_t j = full.find('/', i);
    if (j == std::string::npos) j = full.size();
    const std::string part = full.substr(i, j - i);
    if (part == "..") {
      if (parts.empty()) return false;
      parts.pop_back();
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    i = j + 1;
  }
  out.clear();
  for (const auto& part : parts) out += (out.empty() ? "" : "/") + part;
  if (trailing && !out.empty()) out += '/';
  return true;
}

bool link_target_exists(const fs::path& dist, const std::string& target) {
  if (target == kDistMarker) return false;  // ssg's own, not published content
  if (target.empty()) return fs::is_regular_file(dist / "index.html");
  if (target.back() == '/') {
    return fs::is_regular_file(dist / target.substr(0, target.size() - 1) / "index.html");
  }
  const auto p = dist / target;
  return fs::is_regular_file(p) || fs::is_regular_file(p / "index.html") ||
         fs::is_regular_file(fs::path(p.string() + ".html"));
}

std::string sitemap_xml(const SiteConfig& cfg, const std::vector<Page>& pages) {
  std::vector<const Page*> sorted;
  for (const auto& p : pages) {
    auto it = p.vars.find("sitemap");
    if (it != p.vars.end() && !is_truthy(it->second)) continue;
    sorted.push_back(&p);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const Page* a, const Page* b) { return a->url < b->url; });
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
  for (const Page* p : sorted) {
    out += "  <url><loc>" + html_escape_attr(cfg.base_url + p->url) + "</loc>";
    const auto date = var_or(p->vars, "date");
    if (looks_like_date(date)) out += "<lastmod>" + date.substr(0, 10) + "</lastmod>";
    out += "</url>\n";
  }
  return out + "</urlset>\n";
}

}  // namespace

BuildStats build_site(const SiteConfig& cfg, const BuildOptions& opt) {
  const auto started = std::chrono::steady_clock::now();
  if (!fs::is_directory(cfg.content_dir)) {
    throw std::runtime_error("content dir is not a directory: " + cfg.content_dir.string());
  }
  check_dist(cfg);
  if (!fs::exists(cfg.skeleton_path)) {
    throw std::runtime_error("missing HTML skeleton: " + cfg.skeleton_path.string());
  }

  Ctx c{cfg, opt, {}, {}, {}, {}, 0};
  const std::string skeleton_rel = generic_rel(cfg.skeleton_path, cfg.content_dir);
  Skeleton skeleton;
  try {
    skeleton = load_skeleton(cfg);
  } catch (const std::exception& e) {
    throw std::runtime_error(skeleton_rel + ": " + e.what());
  }

  for (const auto& pat : cfg.ignore) c.rules.add("", pat);
  walk(c, cfg.content_dir, "");

  std::string why;
  const bool ours = own_dist(cfg.dist_dir, why);
  if (cfg.clean && !ours) c.warn("not cleaning " + cfg.dist_dir.string() + ": " + why);
  // remove_all doesn't follow symlinks, so a link in dist can't take anything else with it.
  if (cfg.clean && ours && fs::exists(cfg.dist_dir)) {
    for (const auto& e : fs::directory_iterator(cfg.dist_dir)) fs::remove_all(e.path());
  }
  fs::create_directories(cfg.dist_dir);
  if (ours) {
    write_file(cfg.dist_dir / kDistMarker,
               "Build output of ssg, which empties this directory before each build.\n");
  }

  BuildStats stats;
  for (const auto& rel : c.asset_rels) {
    c.claim(rel, rel);
    const auto dest = cfg.dist_dir / fs::path(rel);
    prepare_dest(cfg.dist_dir, dest);
    fs::copy_file(cfg.content_dir / fs::path(rel), dest, fs::copy_options::overwrite_existing);
    ++stats.files;
  }

  // Pass 1: parse and resolve every page so they can refer to each other. A page whose own
  // frontmatter makes it a draft isn't rendered at all; one made a draft by a template is
  // dropped after rendering. Either way it doesn't count when deciding foo.md vs foo/index.md.
  const std::string today = build_date(c);
  std::vector<Page> pages;
  for (const auto& rel : c.md_rels) {
    Page p;
    p.src = cfg.content_dir / fs::path(rel);
    p.rel = rel;
    try {
      p.doc = parse_markdown_source(read_file(p.src));
    } catch (const std::exception& e) {
      throw std::runtime_error(rel + ": " + e.what());
    }
    const auto draft = p.doc.vars.find("draft");
    if (!cfg.drafts && draft != p.doc.vars.end() && draft->second.find("{{") == std::string::npos &&
        is_truthy(draft->second)) {
      continue;
    }
    pages.push_back(std::move(p));
  }
  // Renders p with url, path and empty nav vars; false if it turns out to be a draft.
  auto first_pass = [&](Page& p, const std::set<std::string>& md_set) {
    p.out_rel = publish_rel(p.rel, md_set);
    p.url = url_for(p.out_rel);
    try {
      Vars defaults = {{"url", p.url},       {"path", lit(p.rel)}, {"build_date", today},
                       {"children", ""},     {"toc", ""},          {"word_count", ""},
                       {"reading_time", ""}, {"prev_url", ""},     {"prev_title", ""},
                       {"next_url", ""},     {"next_title", ""},   {"parent_url", ""},
                       {"parent_title", ""}};
      RenderedDoc r = render_extended(cfg, p.src, p.doc, defaults);
      if (!cfg.drafts && is_truthy(var_or(r.vars, "draft"))) return false;
      p.words = count_words(markdown_to_text(r.markdown, cfg.markdown));
      p.has_headings = false;
      for (const auto& h : markdown_headings(r.markdown, cfg.markdown)) {
        if (h.level >= 2) p.has_headings = true;
      }
      p.vars = std::move(r.vars);
    } catch (const std::exception& e) {
      throw std::runtime_error(p.rel + ": " + e.what());
    }
    return true;
  };
  auto rel_set = [&]() {
    std::set<std::string> out;
    for (const auto& p : pages) out.insert(p.rel);
    return out;
  };
  auto keep_if = [&](auto pred) {
    std::vector<Page> kept;
    for (auto& p : pages) {
      if (pred(p)) kept.push_back(std::move(p));
    }
    const bool dropped = kept.size() != pages.size();
    pages = std::move(kept);
    return dropped;
  };
  const auto md_set = rel_set();
  if (keep_if([&](Page& p) { return first_pass(p, md_set); })) {
    // Template-made drafts are gone: redo the pages whose place that changes.
    const auto kept = rel_set();
    keep_if([&](Page& p) { return publish_rel(p.rel, kept) == p.out_rel || first_pass(p, kept); });
  }
  std::map<std::string, int> by_rel;
  for (size_t i = 0; i < pages.size(); ++i) by_rel[pages[i].rel] = static_cast<int>(i);

  // Parent: dir/index.md for dir/foo.md; parent-dir/index.md for dir/index.md.
  for (size_t i = 0; i < pages.size(); ++i) {
    const auto& rel = pages[i].rel;
    const auto dir = parent_dir(rel);
    std::string parent;
    if (base_name(rel) == "index.md") {
      if (dir.empty()) continue;
      parent = join_rel(parent_dir(dir), "index.md");
    } else {
      parent = join_rel(dir, "index.md");
    }
    auto it = by_rel.find(parent);
    if (it == by_rel.end()) continue;
    pages[i].parent = it->second;
    pages[static_cast<size_t>(it->second)].children.push_back(static_cast<int>(i));
  }
  for (auto& p : pages) {
    std::sort(p.children.begin(), p.children.end(), [&](int a, int b) {
      return page_before(pages[static_cast<size_t>(a)], pages[static_cast<size_t>(b)]);
    });
  }

  // The vars that link page i to its neighbours, given every page's link title. Values are
  // escaped so a literal {{ in a title stays literal when evaluated again.
  auto nav_defaults = [&](size_t i, const std::vector<std::string>& titles) {
    const Page& p = pages[i];
    // Missing neighbours stay unset, so {{prev_url}} etc. stay as written.
    Vars d = {{"url", p.url}, {"path", lit(p.rel)}, {"build_date", today}};
    d["word_count"] = std::to_string(p.words);
    d["reading_time"] = std::to_string(std::max<size_t>(1, (p.words + 199) / 200));
    std::string children;
    for (int ci : p.children) {
      const Page& ch = pages[static_cast<size_t>(ci)];
      if (!children.empty()) children += '\n';
      children += "- [" + md_escape(titles[static_cast<size_t>(ci)]) + "](" + ch.url + ")";
    }
    d["children"] = lit(children);
    if (p.parent < 0) return d;
    const auto par = static_cast<size_t>(p.parent);
    d["parent_url"] = pages[par].url;
    d["parent_title"] = lit(titles[par]);
    const auto& sib = pages[par].children;
    const auto at = static_cast<size_t>(std::find(sib.begin(), sib.end(), static_cast<int>(i)) -
                                        sib.begin());
    if (at > 0) {
      const auto prev = static_cast<size_t>(sib[at - 1]);
      d["prev_url"] = pages[prev].url;
      d["prev_title"] = lit(titles[prev]);
    }
    if (at + 1 < sib.size()) {
      const auto next = static_cast<size_t>(sib[at + 1]);
      d["next_url"] = pages[next].url;
      d["next_title"] = lit(titles[next]);
    }
    return d;
  };

  // Link titles: pass 1's, then once more with the nav vars filled from those, so a title like
  // "{{parent_title}}: A" resolves the same whichever order the pages come in.
  std::vector<std::string> titles(pages.size());
  for (size_t i = 0; i < pages.size(); ++i) titles[i] = nav_title(pages[i].vars, pages[i].rel);
  {
    std::vector<std::string> next(pages.size());
    for (size_t i = 0; i < pages.size(); ++i) {
      Vars defaults = nav_defaults(i, titles);
      defaults["toc"] = "";
      ParsedDoc vars_only = pages[i].doc;
      vars_only.body.clear();  // only the vars matter here
      try {
        next[i] = nav_title(render_extended(cfg, pages[i].src, vars_only, defaults).vars,
                            pages[i].rel);
      } catch (const std::exception& e) {
        throw std::runtime_error(pages[i].rel + ": " + e.what());
      }
    }
    titles = std::move(next);
  }

  // Pass 2: render for real.
  std::vector<std::string> htmls(pages.size());
  for (size_t i = 0; i < pages.size(); ++i) {
    const Page& p = pages[i];
    Vars defaults = nav_defaults(i, titles);
    // Pass 1 rendered with an empty toc; with no headings it stays empty, so {{if toc}} is false.
    defaults["toc"] = p.has_headings ? kTocSentinel : "";

    try {
      RenderedDoc r = render_extended(cfg, p.src, p.doc, defaults);
      std::string md = std::move(r.markdown);
      // Templates usually leave blank lines at the top; they mean nothing to markdown.
      for (size_t nl; (nl = md.find('\n')) != std::string::npos && trim(md.substr(0, nl)).empty();) {
        md.erase(0, nl + 1);
      }
      if (!md.empty() && md.back() != '\n') md.push_back('\n');

      std::string toc_md;
      const bool has_toc = md.find(kTocSentinel) != std::string::npos;
      if (has_toc || skeleton.uses_toc) {
        toc_md = toc_markdown(markdown_headings(replace_all(md, kTocSentinel, ""), cfg.markdown));
      }
      if (has_toc) md = expand_toc(md, toc_md);
      // A var that held {{toc}} is now the sentinel; nothing else can render it.
      for (auto& kv : r.vars) kv.second = replace_all(kv.second, kTocSentinel, "");

      const auto dest = cfg.dist_dir / fs::path(p.out_rel);
      auto html_path = dest, txt_path = dest;
      html_path.replace_extension(".html");
      txt_path.replace_extension(".txt");
      const std::string stem = p.out_rel.substr(0, p.out_rel.size() - 3);
      c.claim(p.out_rel, p.rel);
      c.claim(stem + ".html", p.rel);
      c.claim(stem + ".txt", p.rel);

      prepare_dest(cfg.dist_dir, dest);
      write_file(dest, md);
      const std::string body = markdown_to_html(md, cfg.markdown);
      const std::string toc_html = toc_md.empty() ? "" : markdown_to_html(toc_md, cfg.markdown);
      htmls[i] = render_skeleton(skeleton, r.vars, body, toc_html);
      write_file(html_path, htmls[i]);
      write_file(txt_path, markdown_to_text(md, cfg.markdown));
      pages[i].vars = std::move(r.vars);
    } catch (const std::exception& e) {
      throw std::runtime_error(p.rel + ": " + e.what());
    }
    ++stats.pages;
  }

  if (!cfg.base_url.empty()) {
    if (c.outputs.count("sitemap.xml")) {
      c.warn("sitemap.xml exists in the content dir; not generating one");
    } else {
      c.claim("sitemap.xml", "(sitemap)");
      write_file(cfg.dist_dir / "sitemap.xml", sitemap_xml(cfg, pages));
    }
  }

  if (opt.check_links) {
    for (size_t i = 0; i < pages.size(); ++i) {
      std::set<std::string> seen;
      for (const auto& link : local_links(htmls[i])) {
        if (!seen.insert(link).second) continue;
        std::string target;
        if (!resolve_link(pages[i].url, link, target) || !link_target_exists(cfg.dist_dir, target)) {
          c.warn(pages[i].rel + ": broken link " + link);
        }
      }
    }
  }

  stats.warnings = c.warnings;
  if (!opt.quiet) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count();
    std::cerr << "ssg: built " << stats.pages << " page" << (stats.pages == 1 ? "" : "s")
              << ", copied " << stats.files << " file" << (stats.files == 1 ? "" : "s") << " in "
              << ms << " ms";
    if (c.warnings) std::cerr << " (" << c.warnings << " warning" << (c.warnings == 1 ? "" : "s") << ")";
    std::cerr << "\n";
  }
  return stats;
}
