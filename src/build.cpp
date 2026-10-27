#include "build.hpp"
#include "css.hpp"
#include "document.hpp"
#include "features.hpp"
#include "deploy.hpp"
#include "ignore.hpp"
#include "markdown.hpp"
#include "plugin.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <unistd.h>
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

// A taxonomy term as listed on its index page and on the pages that carry it.
struct TermRef {
  std::string name, url;
  size_t count = 0;
};

struct Page {
  fs::path src;
  std::string rel;      // source, relative to the content dir: "a/b.md"
  std::string out_rel;  // published markdown, relative to dist: "a/b/index.md"
  std::string url;      // "/a/b/"
  ParsedDoc doc;
  Vars vars;  // pass 1
  size_t words = 0;  // words, plus CJK characters
  size_t cjk = 0;
  bool generated = false;  // made by ssg (pagination, taxonomies), not read from a source page
  Vars extra;              // generated pages: their own defaults (term, page_number, ...)
  std::vector<TermRef> terms;  // a taxonomy index page: its terms
  std::vector<int> members;    // a term page: the pages that carry the term, newest first
  bool lists_members = false;  // a term page, even one whose members are all gone
  std::vector<int> listed;     // the children that {{children}} shows
  int parent = -1;
  std::vector<int> children;  // sorted
  bool has_headings = false;  // any ##+ heading in pass 1, i.e. a non-empty toc
};

// A stylesheet embedded by {{inline}}: its CSP hash (base64 SHA-256) and size.
struct InlineStyle {
  std::string sha256;
  size_t size = 0;
};

void prepare_dest(const fs::path& dist, const fs::path& dest);

struct Ctx {
  const SiteConfig& cfg;
  const BuildOptions& opt;
  IgnoreRules rules;
  std::vector<std::string> md_rels;
  std::vector<std::string> asset_rels;
  std::map<std::string, std::string> outputs;  // dist-relative path -> source that wrote it
  size_t warnings = 0;
  std::vector<Diag> diags;
  bool has_redirects = false;          // redirects.txt in the content root: read, not copied
  std::vector<std::string> data_rels;  // data/*.toml and data/*.json, which are read, not copied
  std::set<std::string> written;       // dist-relative files this build produced
  size_t changed = 0, unchanged = 0;   // incremental: files rewritten vs left alone

  // `file` is the source the warning is about, if any; the text printed is the same either way.
  void warn_at(const std::string& file, const std::string& msg) {
    std::cerr << "ssg: " << warning_label() << " " << (file.empty() ? "" : file + ": ") << msg
              << "\n";
    diags.push_back({file, msg});
    ++warnings;
  }
  void warn(const std::string& msg) { warn_at("", msg); }

  std::string dist_rel(const fs::path& p) const {
    return p.lexically_relative(cfg.dist_dir).generic_string();
  }

  // Readies `dest` (inside dist). An incremental build also clears what an earlier build left in
  // the way: a file where a directory is now needed, or a directory where a file is.
  void prepare(const fs::path& dest) {
    if (opt.incremental) {
      fs::path at = cfg.dist_dir;
      const fs::path rel = dest.lexically_relative(cfg.dist_dir);
      for (auto it = rel.begin(); it != rel.end(); ++it) {
        at /= *it;
        const auto st = fs::symlink_status(at);
        const bool last = std::next(it) == rel.end();
        if (last && fs::is_directory(st) && !fs::is_symlink(st)) fs::remove_all(at);
        else if (!last && fs::exists(st) && !fs::is_directory(st) && !fs::is_symlink(st) &&
                 !written.count(dist_rel(at))) fs::remove(at);
      }
    }
    prepare_dest(cfg.dist_dir, dest);
  }

  // Writes `data` to `dest` in dist. An incremental build leaves a file with the same bytes
  // untouched, so its mtime survives.
  void put(const fs::path& dest, std::string_view data) {
    written.insert(dist_rel(dest));
    if (opt.incremental && fs::is_regular_file(fs::symlink_status(dest)) &&
        fs::file_size(dest) == data.size() && read_file(dest) == data) {
      ++unchanged;
      return;
    }
    write_file(dest, data);
    ++changed;
  }
  void put_copy(const fs::path& src, const fs::path& dest) {
    if (!opt.incremental) {
      written.insert(dist_rel(dest));
      fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
      ++changed;
      return;
    }
    put(dest, read_file(src));
  }

  // After an incremental build: removes what the build did not produce, then empty directories.
  void prune() {
    std::vector<fs::path> dirs, stale;
    for (auto it = fs::recursive_directory_iterator(cfg.dist_dir); it != fs::end(it); ++it) {
      const auto st = it->symlink_status();
      if (fs::is_directory(st) && !fs::is_symlink(st)) dirs.push_back(it->path());
      else if (!written.count(dist_rel(it->path()))) stale.push_back(it->path());
    }
    for (const auto& f : stale) fs::remove(f);
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
      return a.native().size() > b.native().size();
    });
    for (const auto& d : dirs) {
      if (fs::is_empty(d)) fs::remove(d);
    }
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

// Han ideographs, kana and the CJK extension blocks: scripts written without spaces between words.
bool is_cjk(unsigned cp) {
  return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFF66 && cp <= 0xFF9F) || (cp >= 0x20000 && cp <= 0x2FA1F);
}

struct TextCount {
  size_t words = 0;  // whitespace-separated runs, not counting CJK characters
  size_t cjk = 0;    // CJK characters, each counted on its own
};

TextCount count_text(std::string_view text) {
  TextCount n;
  bool in_word = false, has_alnum = false;
  auto end_word = [&]() {
    if (in_word && has_alnum) ++n.words;
    in_word = has_alnum = false;
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const auto u = static_cast<unsigned char>(text[i]);
    if (std::isspace(u)) {
      end_word();
      continue;
    }
    if (u >= 0xE3 && u <= 0xF0 && i + 2 < text.size()) {
      // A well-formed 3- or 4-byte sequence is decoded to see whether it is CJK.
      const size_t len = u >= 0xF0 ? 4 : 3;
      unsigned cp = u & (len == 4 ? 0x07u : 0x0Fu);
      bool ok = i + len <= text.size();
      for (size_t k = 1; ok && k < len; ++k) {
        const auto b = static_cast<unsigned char>(text[i + k]);
        ok = (b & 0xC0) == 0x80;
        cp = (cp << 6) | (b & 0x3Fu);
      }
      if (ok && is_cjk(cp)) {
        end_word();
        ++n.cjk;
        i += len - 1;
        continue;
      }
    }
    in_word = true;
    if (std::isalnum(u) || u >= 0x80) has_alnum = true;
  }
  end_word();
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
                            const std::string& toc_html, const AssetHooks& hooks) {
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
    } else if (starts_with(t, "asset:") && hooks.asset) {
      out += hooks.asset(t.substr(6));
    } else if (starts_with(t, "inline:") && hooks.inline_css) {
      out += hooks.inline_css(t.substr(7));
    } else if (starts_with(t, "extend:") || starts_with(t, "include:") ||
               starts_with(t, "asset:") || starts_with(t, "inline:")) {
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

// Where manifest.json goes: the configured path, else beside dist.
fs::path manifest_target(const SiteConfig& cfg) {
  if (!cfg.write_manifest) return {};
  return cfg.manifest_path.empty() ? cfg.dist_dir.parent_path() / "manifest.json"
                                   : cfg.manifest_path;
}

// Whether the file is one an earlier build wrote, so it is ours to skip or replace.
bool is_ssg_manifest(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  char buf[256];
  in.read(buf, sizeof buf);
  const std::string head(buf, static_cast<size_t>(in.gcount()));
  return head.rfind("{\n  \"schema\": ", 0) == 0 && head.find("\"ssg_version\"") != std::string::npos;
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
    if (std::find(c.cfg.skip_dirs.begin(), c.cfg.skip_dirs.end(), p) != c.cfg.skip_dirs.end()) {
      continue;
    }
    if (p == c.cfg.config_path || (rel_dir.empty() && name == "site.toml")) continue;
    // A manifest an earlier build wrote never ships, whichever build (or --output) wrote it.
    if (!is_dir && (name == "manifest.json" || p == c.cfg.manifest_path) && is_ssg_manifest(p)) {
      continue;
    }

    // The adapter files the deploy target generates are the host's, not content.
    if (!is_dir && rel_dir.empty()) {
      const auto owned = deploy_project_files(c.cfg.deploy_target);
      if (std::find(owned.begin(), owned.end(), name) != owned.end()) continue;
    }

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
    // A link to a file could pull anything readable (keys, /etc) into a public dist.
    if (e.is_symlink() && !c.cfg.follow_symlinks) {
      if (!c.opt.quiet && c.opt.notes) {
        std::cerr << "ssg: note: skipping " << rel
                  << " (a symlink; set follow_symlinks = true to copy what it points to)\n";
      }
      continue;
    }
    if (rel_dir.empty() && name == "redirects.txt") {
      c.has_redirects = true;
      continue;
    }
    if (rel_dir == "data" && (ends_with(name, ".toml") || ends_with(name, ".json"))) {
      c.data_rels.push_back(rel);
      continue;
    }
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

// A date after the build date holds the page back, unless drafts or `future = true` say otherwise.
bool is_scheduled(const SiteConfig& cfg, std::string_view date, const std::string& today) {
  return !cfg.drafts && !cfg.future && looks_like_date(date) && date.substr(0, 10) > today;
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

// A page URL or link target without slashes at the ends or a trailing "index.html" / ".html",
// so every spelling of a page's address compares equal.
std::string page_key(std::string key) {
  while (!key.empty() && key.front() == '/') key.erase(0, 1);
  if (ends_with(key, "index.html")) key.resize(key.size() - 10);
  else if (ends_with(key, ".html")) key.resize(key.size() - 5);
  while (!key.empty() && key.back() == '/') key.pop_back();
  return key;
}

// <img> tags with no alt attribute or an empty one.
size_t imgs_without_alt(const std::string& html) {
  size_t n = 0;
  for (size_t at = find_ci(html, "<img", 0); at != std::string::npos;
       at = find_ci(html, "<img", at + 4)) {
    const size_t end = html.find('>', at);
    if (end == std::string::npos) break;
    const std::string_view tag(html.data() + at, end - at);
    const size_t alt = find_ci(tag, " alt=", 0);
    const bool has_text = alt != std::string_view::npos && alt + 5 < tag.size() &&
                          !(tag[alt + 5] == '"' && alt + 6 < tag.size() && tag[alt + 6] == '"');
    if (!has_text) ++n;
  }
  return n;
}

std::string sitemap_xml(const SiteConfig& cfg, const std::vector<Page>& pages) {
  std::vector<const Page*> sorted;
  for (const auto& p : pages) {
    auto it = p.vars.find("sitemap");
    if (it != p.vars.end() && !is_truthy(it->second)) continue;
    if (is_truthy(var_or(p.vars, "noindex")) || page_key(p.url) == "404") continue;
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

// Hands every ::: container that a plugin handles (hook container:<name>) to that plugin and
// puts the Markdown it returns in the container's place. Containers are matched the way the
// parser does: a closing line ends the innermost open one. Fenced code is left alone, and a
// plugin's answer is not scanned again.
std::string expand_containers(const std::string& md, const std::string& page, PluginHost& host) {
  auto colons = [](std::string_view l, size_t& rest) {
    size_t ind = 0;
    while (ind < l.size() && ind < 4 && l[ind] == ' ') ++ind;
    size_t n = 0;
    while (ind + n < l.size() && l[ind + n] == ':') ++n;
    rest = ind + n;
    return ind < 4 ? n : size_t(0);
  };
  std::vector<std::string> lines;
  for (size_t pos = 0; pos < md.size();) {
    size_t nl = md.find('\n', pos);
    if (nl == std::string::npos) nl = md.size();
    lines.push_back(md.substr(pos, nl - pos));
    pos = nl + 1;
  }
  std::string out;
  char fence = 0;
  size_t fence_len = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& l = lines[i];
    const std::string_view t = ltrim(l);
    if (fence) {
      size_t n = 0;
      while (n < t.size() && t[n] == fence) ++n;
      if (l.size() - t.size() < 4 && n >= fence_len && trim(t.substr(n)).empty()) fence = 0;
    } else if (l.size() - t.size() < 4 && (starts_with(t, "```") || starts_with(t, "~~~"))) {
      fence = t[0];
      fence_len = 0;
      while (fence_len < t.size() && t[fence_len] == fence) ++fence_len;
    }
    size_t rest = 0;
    const size_t open = fence ? 0 : colons(l, rest);
    std::string name, args;
    if (open >= 3) {
      std::string_view r = trim(std::string_view(l).substr(rest));
      size_t e = 0;
      while (e < r.size() && !std::isspace(static_cast<unsigned char>(r[e]))) ++e;
      for (char ch : r.substr(0, e)) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') {
          name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
      }
      args = std::string(trim(r.substr(e)));
    }
    if (name.empty() || !host.wants("container:" + name)) {
      out += l;
      if (i + 1 < lines.size() || (!md.empty() && md.back() == '\n')) out += '\n';
      continue;
    }
    // Find the line that closes this container, minding nested ones.
    std::vector<size_t> stack{open};
    std::string body;
    size_t j = i + 1;
    char f2 = 0;
    size_t f2len = 0;
    for (; j < lines.size(); ++j) {
      const std::string_view u = ltrim(lines[j]);
      const bool shallow = lines[j].size() - u.size() < 4;
      if (f2) {
        size_t n = 0;
        while (n < u.size() && u[n] == f2) ++n;
        if (shallow && n >= f2len && trim(u.substr(n)).empty()) f2 = 0;
      } else if (shallow && (starts_with(u, "```") || starts_with(u, "~~~"))) {
        f2 = u[0];
        f2len = 0;
        while (f2len < u.size() && u[f2len] == f2) ++f2len;
      } else {
        size_t r2 = 0;
        const size_t n = colons(lines[j], r2);
        if (n >= 3) {
          if (trim(std::string_view(lines[j]).substr(r2)).empty()) {
            if (n >= stack.back()) stack.pop_back();
            if (stack.empty()) break;
          } else {
            stack.push_back(n);
          }
        }
      }
      body += lines[j] + "\n";
    }
    Json params = Json::make_object();
    params.set("name", Json(name)).set("args", Json(args)).set("body", Json(body)).set("page", Json(page));
    std::string replacement;
    if (!host.container(name, params, replacement)) {
      // The plugin was dropped (required = false): keep the container as written.
      for (size_t k = i; k < std::min(j + 1, lines.size()); ++k) out += lines[k] + "\n";
    } else {
      if (!out.empty() && out.compare(out.size() >= 2 ? out.size() - 2 : 0, 2, "\n\n") != 0) out += '\n';
      out += replacement;
      if (replacement.empty() || replacement.back() != '\n') out += '\n';
      out += '\n';
    }
    i = j;
  }
  return out;
}

// A scratch directory that goes away with the build.
struct ScratchDir {
  fs::path path;
  ~ScratchDir() {
    std::error_code ec;
    if (!path.empty()) fs::remove_all(path, ec);
  }
  const fs::path& make() {
    if (path.empty()) {
      std::string tmpl = (fs::temp_directory_path() / "ssg-plugin-XXXXXX").string();
      if (!mkdtemp(tmpl.data())) throw std::runtime_error("cannot create a scratch directory");
      path = tmpl;
    }
    return path;
  }
};

const char* content_type(const char* ext) {
  return std::string_view(ext) == "html" ? "text/html; charset=utf-8"
         : std::string_view(ext) == "md" ? "text/markdown; charset=utf-8"
                                         : "text/plain; charset=utf-8";
}

// manifest.json, schema 1. Fields are only ever added within a schema. Source paths are
// relative to the content dir; output paths to dist. No timestamps but the build date.
std::string manifest_json(const SiteConfig& cfg, const std::string& version, const std::string& date,
                          const std::vector<Page>& pages, const std::vector<std::string>& assets,
                          const std::map<std::string, uintmax_t>& sizes,
                          const std::vector<Redirect>& redirects,
                          const std::map<std::string, std::string>& fingerprints,
                          const std::map<std::string, InlineStyle>& inlined,
                          const std::vector<PluginInfo>& plugins) {
  std::string o = "{\n  \"schema\": 1,\n  \"ssg_version\": " + json_str(version) +
                  ",\n  \"build_date\": " + json_str(date) +
                  ",\n  \"base_url\": " + json_str(cfg.base_url) + ",\n  \"pages\": [";
  for (size_t i = 0; i < pages.size(); ++i) {
    const Page& p = pages[i];
    const std::string stem = p.out_rel.substr(0, p.out_rel.size() - 3);
    o += std::string(i ? "," : "") + "\n    {\"url\": " + json_str(p.url) +
         ", \"source\": " + json_str(p.rel) + ", \"title\": " + json_str(nav_title(p.vars, p.rel)) +
         ", \"date\": " + json_str(var_or(p.vars, "date")) + ",\n     \"outputs\": [";
    int n = 0;
    for (const char* ext : {"html", "md", "txt"}) {
      o += std::string(n++ ? ", " : "") + "{\"path\": " + json_str(stem + "." + ext) +
           ", \"format\": " + json_str(ext) + ", \"content_type\": " +
           json_str(content_type(ext)) + "}";
    }
    o += "]}";
  }
  o += std::string(pages.empty() ? "" : "\n  ") + "],\n  \"assets\": [";
  for (size_t i = 0; i < assets.size(); ++i) {
    // `output` is where the file is published; it differs from `path` once fingerprinted.
    o += std::string(i ? "," : "") + "\n    {\"path\": " + json_str(assets[i]) +
         ", \"output\": " + json_str(assets[i]) + ", \"size\": " + std::to_string(sizes.at(assets[i]));
    // Also published, next to the plain copy; its name never changes unless the content does.
    if (const auto fp = fingerprints.find(assets[i]); fp != fingerprints.end()) {
      o += ", \"fingerprint\": " + json_str(fp->second) + ", \"fingerprint_size\": " +
           std::to_string(sizes.at(fp->second)) + ", \"immutable\": true";
    }
    o += "}";
  }
  o += std::string(assets.empty() ? "" : "\n  ") + "],\n  \"inline_styles\": [";
  size_t n_inline = 0;
  for (const auto& kv : inlined) {
    o += std::string(n_inline++ ? "," : "") + "\n    {\"path\": " + json_str(kv.first) +
         ", \"size\": " + std::to_string(kv.second.size) + ", \"csp_hash\": " +
         json_str("sha256-" + kv.second.sha256) + "}";
  }
  o += std::string(inlined.empty() ? "" : "\n  ") + "],\n  \"redirects\": [";
  for (size_t k = 0; k < redirects.size(); ++k) {
    o += std::string(k ? "," : "") + "\n    {\"from\": " + json_str(redirects[k].from) +
         ", \"to\": " + json_str(redirects[k].to) +
         ", \"status\": " + std::to_string(redirects[k].status) + "}";
  }
  o += std::string(redirects.empty() ? "" : "\n  ") + "],\n  \"plugins\": [";
  size_t i = 0;
  for (const PluginInfo& p : plugins) {
    o += std::string(i++ ? ", " : "") + "{\"name\": " + json_str(p.name) + ", \"version\": " +
         json_str(p.version) + ", \"protocol\": " + std::to_string(p.protocol) + "}";
  }
  return o + "]\n}\n";
}

BuildStats build_site(const SiteConfig& cfg, const BuildOptions& opt) {
  const auto started = std::chrono::steady_clock::now();
  if (!fs::is_directory(cfg.content_dir)) {
    throw std::runtime_error("content dir is not a directory: " + cfg.content_dir.string());
  }
  check_dist(cfg);
  if (!fs::exists(cfg.skeleton_path)) {
    throw std::runtime_error("missing HTML skeleton: " + cfg.skeleton_path.string());
  }

  Ctx c{cfg, opt, {}, {}, {}, {}, 0, {}, false, {}, {}};
  const std::string skeleton_rel = generic_rel(cfg.skeleton_path, cfg.content_dir);
  Skeleton skeleton;
  try {
    skeleton = load_skeleton(cfg);
  } catch (const std::exception& e) {
    throw std::runtime_error(skeleton_rel + ": " + e.what());
  }

  for (const auto& pat : cfg.ignore) c.rules.add("", pat);
  walk(c, cfg.content_dir, "");

  // Plugins start before dist is touched, so a missing one fails the build with dist intact.
  const std::string today = build_date(c);
  PluginHost plugins(cfg, [&](const std::string& m) { c.warn(m); }, !opt.quiet);
  if (!opt.no_plugins && !cfg.plugin_order.empty()) {
    Json site = Json::make_object();
    site.set("ssg_version", Json(opt.version)).set("protocol", Json(kPluginProtocol))
        .set("base_url", Json(cfg.base_url)).set("content_dir", Json(cfg.content_dir.string()))
        .set("dist_dir", Json(cfg.dist_dir.string())).set("build_date", Json(today));
    plugins.start(site);
  }
  ScratchDir scratch;

  std::string why;
  const bool ours = own_dist(cfg.dist_dir, why);
  if (cfg.clean && !ours) c.warn("not cleaning " + cfg.dist_dir.string() + ": " + why);
  // remove_all doesn't follow symlinks, so a link in dist can't take anything else with it.
  if (cfg.clean && ours && !opt.incremental && fs::exists(cfg.dist_dir)) {
    for (const auto& e : fs::directory_iterator(cfg.dist_dir)) fs::remove_all(e.path());
  }
  fs::create_directories(cfg.dist_dir);
  if (ours) {
    c.put(cfg.dist_dir / kDistMarker,
               "Build output of ssg, which empties this directory before each build.\n");
  }

  // data/*.toml and *.json, as {{data.<file>.<key>}}: the lowest-priority vars of every page.
  Vars data_vars;
  std::set<std::string> stems;
  for (const auto& rel : c.data_rels) {
    const bool is_json = ends_with(rel, ".json");
    const std::string stem = base_name(rel).substr(0, base_name(rel).size() - 5);
    if (!std::all_of(stem.begin(), stem.end(), [](char ch) {
          return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-';
        })) {
      c.warn_at(rel, "not read as data: use only letters, digits, - and _ in the file name");
      continue;
    }
    const std::string prefix = "data." + stem;
    if (!stems.insert(stem).second) c.warn_at(rel, "replaces the keys of another " + stem + " data file");
    std::vector<std::string> skipped;
    Vars file_vars;
    try {
      flatten_data(read_file(cfg.content_dir / fs::path(rel)), is_json, prefix, file_vars, skipped);
    } catch (const std::exception& e) {
      throw std::runtime_error(rel + ": " + e.what());
    }
    // Values are text, not templates: a literal {{ in a data file stays literal.
    for (auto& kv : file_vars) data_vars[kv.first] = lit(kv.second);
    for (const auto& k : skipped) c.warn_at(rel, "key '" + k + "' is not usable in a template; skipped");
  }

  BuildStats stats;
  std::map<std::string, uintmax_t> asset_sizes;
  for (const auto& rel : c.asset_rels) {
    c.claim(rel, rel);
    const auto dest = cfg.dist_dir / fs::path(rel);
    c.prepare(dest);
    if (cfg.css.minify && ends_with(rel, ".css")) {
      c.put(dest, minify_css(read_file(cfg.content_dir / fs::path(rel))));
    } else {
      c.put_copy(cfg.content_dir / fs::path(rel), dest);
    }
    asset_sizes[rel] = fs::file_size(dest);
    stats.file_list.push_back(rel);
    if (opt.verbose) std::cerr << "ssg: copied " << rel << "\n";
    ++stats.files;
  }

  // The asset hook: a plugin may publish more files for an asset (resized images, say). They
  // join the asset list, so {{asset}} and the manifest know them like any other file.
  if (plugins.wants("asset")) {
    const std::vector<std::string> originals = c.asset_rels;
    const fs::path out_dir = scratch.make();
    for (const auto& rel : originals) {
      for (const PluginFile& f : plugins.assets(rel, cfg.content_dir / fs::path(rel), out_dir)) {
        c.claim(f.path, rel + " (plugin)");
        const auto dest = cfg.dist_dir / fs::path(f.path);
        c.prepare(dest);
        c.put_copy(f.file, dest);
        asset_sizes[f.path] = fs::file_size(dest);
        if (std::find(c.asset_rels.begin(), c.asset_rels.end(), f.path) == c.asset_rels.end()) {
          c.asset_rels.push_back(f.path);
        }
        stats.file_list.push_back(f.path);
        if (opt.verbose) std::cerr << "ssg: plugin wrote " << f.path << " for " << rel << "\n";
        ++stats.files;
      }
    }
  }

  // {{asset "x"}} and {{inline "x"}}. Both read the file as published (minified if that is on),
  // so a fingerprint and a CSP hash always describe what a visitor gets.
  const std::set<std::string> asset_set(c.asset_rels.begin(), c.asset_rels.end());
  std::map<std::string, std::string> fingerprints;  // asset -> fingerprinted name
  std::map<std::string, InlineStyle> inlined;       // asset -> what was embedded
  auto known_asset = [&](const std::string& name, const char* tag) {
    const std::string rel = fs::path(name).lexically_normal().generic_string();
    if (!asset_set.count(rel) || name != rel) {
      throw std::runtime_error(std::string("{{") + tag + " \"" + name +
                               "\"}}: no such file among the published assets (give a path " +
                               "relative to the content dir)");
    }
    return rel;
  };
  AssetHooks hooks;
  hooks.asset = [&](const std::string& name) {
    const std::string rel = known_asset(name, "asset");
    auto it = fingerprints.find(rel);
    if (it == fingerprints.end()) {
      const std::string hash = sha256_hex(read_file(cfg.dist_dir / fs::path(rel))).substr(0, 10);
      const std::string dir = parent_dir(rel), base = base_name(rel);
      const size_t dot = base.rfind('.');
      const std::string hashed =
          join_rel(dir, dot == std::string::npos || dot == 0
                            ? base + "." + hash
                            : base.substr(0, dot) + "." + hash + base.substr(dot));
      c.claim(hashed, rel);
      const auto dest = cfg.dist_dir / fs::path(hashed);
      c.prepare(dest);
      c.put_copy(cfg.dist_dir / fs::path(rel), dest);
      asset_sizes[hashed] = fs::file_size(dest);
      stats.file_list.push_back(hashed);
      if (opt.verbose) std::cerr << "ssg: fingerprinted " << rel << " as " << hashed << "\n";
      ++stats.files;
      it = fingerprints.emplace(rel, hashed).first;
    }
    return "/" + url_encode_path(it->second);
  };
  hooks.inline_css = [&](const std::string& name) {
    const std::string rel = known_asset(name, "inline");
    if (!ends_with(rel, ".css")) {
      throw std::runtime_error("{{inline \"" + name + "\"}}: only .css files can be inlined");
    }
    std::string css = read_file(cfg.dist_dir / fs::path(rel));
    css = std::string(rtrim(css));
    if (!inlined.count(rel)) {
      std::string lower = css;
      for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (lower.find("</style") != std::string::npos) {
        throw std::runtime_error("{{inline \"" + name + "\"}}: the file contains \"</style\", " +
                                 "which would end the <style> element early");
      }
      if (cfg.css.inline_limit && css.size() > cfg.css.inline_limit) {
        c.warn_at(rel, "inlined stylesheet is " + std::to_string(css.size()) +
                           " bytes, over css.inline_limit (" +
                           std::to_string(cfg.css.inline_limit) + ")");
      }
      inlined[rel] = {sha256_base64(css), css.size()};
    }
    return css;
  };

  // Pass 1: parse and resolve every page so they can refer to each other. A page whose own
  // frontmatter makes it a draft isn't rendered at all; one made a draft by a template is
  // dropped after rendering. Either way it doesn't count when deciding foo.md vs foo/index.md.
  std::vector<Page> pages;
  // The address of the Atom feed, for <link rel="alternate"> in the skeleton; "" if there is none.
  const std::string feed_url = cfg.feed.enabled && !cfg.base_url.empty() ? cfg.base_url + "/feed.xml" : "";
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
    const auto date = p.doc.vars.find("date");
    if (date != p.doc.vars.end() && date->second.find("{{") == std::string::npos &&
        is_scheduled(cfg, date->second, today)) {
      continue;
    }
    pages.push_back(std::move(p));
  }
  // Renders p with url, path and empty nav vars; false if it turns out to be a draft.
  auto first_pass = [&](Page& p, const std::set<std::string>& md_set) {
    if (!p.generated) {
      p.out_rel = publish_rel(p.rel, md_set);
      p.url = url_for(p.out_rel);
    }
    try {
      Vars builtin = {{"url", p.url},       {"path", lit(p.rel)}, {"build_date", today},
                       {"children", ""},     {"toc", ""},          {"word_count", ""},
                       {"reading_time", ""}, {"prev_url", ""},     {"prev_title", ""},
                       {"next_url", ""},     {"next_title", ""},   {"parent_url", ""},
                       {"parent_title", ""}, {"og_title", ""},  {"og_description", ""}};
      if (!cfg.base_url.empty()) {
        builtin["absolute_url"] = builtin["canonical_url"] = cfg.base_url + p.url;
      }
      if (!feed_url.empty()) builtin["feed_url"] = feed_url;
      for (const auto& tax : cfg.taxonomies) builtin[tax.name + "_links"] = "";
      Vars defaults = data_vars;
      for (const auto& kv : builtin) defaults[kv.first] = kv.second;
      for (const auto& kv : p.extra) defaults[kv.first] = kv.second;
      RenderedDoc r = render_extended(cfg, p.src, p.doc, defaults, &hooks);
      if (!cfg.drafts && is_truthy(var_or(r.vars, "draft"))) return false;
      if (is_scheduled(cfg, var_or(r.vars, "date"), today)) return false;
      const MarkdownDoc parsed(r.markdown, cfg.markdown);
      const TextCount tc = count_text(parsed.text());
      p.words = tc.words + tc.cjk;
      p.cjk = tc.cjk;
      p.has_headings = false;
      for (const auto& h : parsed.headings()) {
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
    p.listed = p.children;
  }

  // Taxonomies: an index page and a page per term, made from templates/taxonomy.md and term.md
  // (or a bare list when those are missing). Terms come from the pass-1 value of the page's key.
  std::map<std::string, std::map<std::string, std::string>> page_links;  // page rel -> key -> md
  const size_t real_pages = pages.size();
  // Adds a taxonomy index (term = false) or term page whose rel and extra vars are set. The
  // source is templates/taxonomy.md or term.md, else a bare list.
  auto add_generated = [&](Page g, bool term) {
    const char* what = term ? "term" : "taxonomy index";
    // A hand-written tags.md is published as tags/index.md, so compare where pages end up.
    if (std::any_of(pages.begin(), pages.end(), [&](const Page& o) { return o.out_rel == g.rel; })) {
      c.warn_at(g.rel, std::string("exists as a page; not generating its ") + what + " page");
      return;
    }
    g.generated = true;
    g.out_rel = g.rel;
    g.url = url_for(g.rel);
    g.src = cfg.templates_dir / (term ? "term.md" : "taxonomy.md");
    try {
      g.doc = parse_markdown_source(fs::exists(g.src) ? read_file(g.src) : term ? "{{pages}}\n" : "{{terms}}\n");
    } catch (const std::exception& e) {
      throw std::runtime_error(g.src.filename().string() + ": " + e.what());
    }
    if (!first_pass(g, md_set)) return;
    by_rel[g.rel] = static_cast<int>(pages.size());
    pages.push_back(std::move(g));
  };
  for (const TaxonomyConfig& tax : cfg.taxonomies) {
    struct Term {
      std::string name;
      std::vector<int> pages;
    };
    std::map<std::string, Term> found;  // slug -> term
    for (size_t i = 0; i < real_pages; ++i) {
      const std::string value = var_or(pages[i].vars, tax.key);
      size_t at = 0;
      while (at <= value.size()) {
        size_t comma = value.find(',', at);
        if (comma == std::string::npos) comma = value.size();
        const std::string name(trim(std::string_view(value).substr(at, comma - at)));
        at = comma + 1;
        if (name.empty()) continue;
        const std::string slug = slugify(name);
        if (slug.empty()) {
          c.warn_at(pages[i].rel, "'" + name + "' in " + tax.key + " has no letters or digits for a URL; skipped");
          continue;
        }
        Term& t = found[slug];
        if (t.name.empty()) t.name = name;
        if (t.pages.empty() || t.pages.back() != static_cast<int>(i)) t.pages.push_back(static_cast<int>(i));
      }
    }
    if (found.empty()) continue;
    std::vector<TermRef> refs;
    for (auto& kv : found) {
      auto& mem = kv.second.pages;
      std::sort(mem.begin(), mem.end(), [&](int a, int b) {
        const auto da = var_or(pages[static_cast<size_t>(a)].vars, "date");
        const auto db = var_or(pages[static_cast<size_t>(b)].vars, "date");
        return da != db ? da > db : pages[static_cast<size_t>(a)].url < pages[static_cast<size_t>(b)].url;
      });
      refs.push_back({kv.second.name, url_for(tax.path + "/" + kv.first + "/index.md"), mem.size()});
    }
    // Each page's own terms, for {{<name>_links}}.
    size_t k = 0;
    for (const auto& kv : found) {
      for (int pi : kv.second.pages) {
        std::string& md = page_links[pages[static_cast<size_t>(pi)].rel][tax.name];
        md += std::string(md.empty() ? "" : "\n") + "- [" + md_escape(refs[k].name) + "](" + refs[k].url + ")";
      }
      ++k;
    }
    Page index;
    index.rel = tax.path + "/index.md";
    index.extra = {{"taxonomy", tax.name}, {"taxonomy_title", lit(tax.title)},
                   {"title", lit(tax.title)}, {"terms", ""}, {"term_count", std::to_string(refs.size())}};
    index.terms = refs;
    add_generated(std::move(index), false);
    k = 0;
    for (const auto& kv : found) {
      Page term;
      term.rel = tax.path + "/" + kv.first + "/index.md";
      term.extra = {{"taxonomy", tax.name},
                    {"taxonomy_title", lit(tax.title)},
                    {"taxonomy_url", url_for(tax.path + "/index.md")},
                    {"term", lit(kv.second.name)},
                    {"term_slug", kv.first},
                    {"term_url", refs[k].url},
                    {"term_count", std::to_string(kv.second.pages.size())},
                    {"title", lit(kv.second.name)},
                    {"pages", ""}};
      term.members = kv.second.pages;
      term.lists_members = true;
      add_generated(std::move(term), true);
      ++k;
    }
  }
  for (auto& p : pages) {
    const auto it = page_links.find(p.rel);
    if (it == page_links.end() || p.generated) continue;
    for (const auto& kv : it->second) p.extra[kv.first + "_links"] = lit(kv.second);
  }

  // Pagination: `paginate: "N"` on an index page lists N children per page. Page 1 is the index
  // itself; the others are copies at <dir>/page/K/. Each copy shows its own slice of the children.
  for (size_t i = 0; i < real_pages; ++i) {
    if (!pages[i].vars.count("paginate")) continue;
    const std::string raw(trim(var_or(pages[i].vars, "paginate")));
    const std::string rel = pages[i].rel;
    char* end = nullptr;
    const long per = std::strtol(raw.c_str(), &end, 10);
    if (raw.empty() || *end != '\0' || per < 1 || per > 100000) {
      c.warn_at(rel, "paginate must be a whole number from 1 to 100000, not '" + raw + "'; ignored");
      continue;
    }
    if (base_name(rel) != "index.md") {
      c.warn_at(rel, "paginate only works on an index page (index.md); ignored");
      continue;
    }
    const std::vector<int> all = pages[i].children;
    const size_t step = static_cast<size_t>(per);
    const size_t count = std::max<size_t>(1, (all.size() + step - 1) / step);
    const std::string dir = parent_dir(rel);
    auto slice = [&](size_t k) {  // k from 1
      const size_t from = std::min(all.size(), (k - 1) * step);
      return std::vector<int>(all.begin() + static_cast<std::ptrdiff_t>(from),
                              all.begin() + static_cast<std::ptrdiff_t>(std::min(all.size(), from + step)));
    };
    auto page_url = [&](size_t k) {
      return k == 1 ? pages[i].url : url_for(join_rel(dir, "page/" + std::to_string(k) + "/index.md"));
    };
    auto nav_extra = [&](size_t k) {
      return Vars{{"page_number", std::to_string(k)},
                  {"page_count", std::to_string(count)},
                  {"prev_page_url", k > 1 ? page_url(k - 1) : ""},
                  {"next_page_url", k < count ? page_url(k + 1) : ""}};
    };
    pages[i].listed = slice(1);
    for (const auto& kv : nav_extra(1)) pages[i].extra[kv.first] = kv.second;
    std::vector<Page> more;
    for (size_t k = 2; k <= count; ++k) {
      Page g;
      g.rel = join_rel(dir, "page/" + std::to_string(k) + "/index.md");
      g.generated = true;
      g.out_rel = g.rel;
      g.url = url_for(g.rel);
      if (std::any_of(pages.begin(), pages.end(), [&](const Page& o) { return o.out_rel == g.rel; })) {
        c.warn_at(g.rel, "exists as a page; not generating page " + std::to_string(k) + " of " + rel);
        continue;
      }
      g.src = pages[i].src;
      g.doc = pages[i].doc;
      g.parent = pages[i].parent;
      g.listed = slice(k);
      g.extra = nav_extra(k);
      if (!first_pass(g, md_set)) continue;
      more.push_back(std::move(g));
    }
    for (Page& g : more) {
      by_rel[g.rel] = static_cast<int>(pages.size());
      pages.push_back(std::move(g));
    }
  }

  // The vars that link page i to its neighbours, given every page's link title. Values are
  // escaped so a literal {{ in a title stays literal when evaluated again.
  auto nav_defaults = [&](size_t i, const std::vector<std::string>& titles) {
    const Page& p = pages[i];
    // Missing neighbours stay unset, so {{prev_url}} etc. stay as written.
    Vars d = data_vars;
    d["url"] = p.url;
    d["path"] = lit(p.rel);
    d["build_date"] = today;
    if (!feed_url.empty()) d["feed_url"] = feed_url;
    d["word_count"] = std::to_string(p.words);
    // 200 words or 500 CJK characters a minute: ceil((5 * words + 2 * cjk) / 1000).
    const size_t latin = p.words - p.cjk;
    d["reading_time"] = std::to_string(std::max<size_t>(1, (5 * latin + 2 * p.cjk + 999) / 1000));
    if (!cfg.base_url.empty()) d["absolute_url"] = d["canonical_url"] = cfg.base_url + p.url;
    // A page's title, or the link title it would get in navigation; description as written.
    const std::string own_title(trim(var_or(p.vars, "title")));
    d["og_title"] = lit(own_title.empty() ? nav_title(p.vars, p.rel) : own_title);
    d["og_description"] = lit(trim(var_or(p.vars, "description")));
    std::string children;
    for (int ci : p.listed) {
      const Page& ch = pages[static_cast<size_t>(ci)];
      if (!children.empty()) children += '\n';
      children += "- [" + md_escape(titles[static_cast<size_t>(ci)]) + "](" + ch.url + ")";
    }
    d["children"] = lit(children);
    for (const auto& tax : cfg.taxonomies) d[tax.name + "_links"] = "";
    for (const auto& kv : p.extra) d[kv.first] = kv.second;
    if (!p.terms.empty()) {
      std::string list;
      for (const TermRef& t : p.terms) {
        list += std::string(list.empty() ? "" : "\n") + "- [" + md_escape(t.name) + "](" + t.url +
                ") (" + std::to_string(t.count) + ")";
      }
      d["terms"] = lit(list);
    }
    if (p.lists_members) {
      std::string list;
      for (int mi : p.members) {
        list += std::string(list.empty() ? "" : "\n") + "- [" +
                md_escape(titles[static_cast<size_t>(mi)]) + "](" + pages[static_cast<size_t>(mi)].url + ")";
      }
      d["pages"] = lit(list);
    }
    if (p.parent < 0) return d;
    const auto par = static_cast<size_t>(p.parent);
    d["parent_url"] = pages[par].url;
    d["parent_title"] = lit(titles[par]);
    const auto& sib = pages[par].children;
    const auto at = static_cast<size_t>(std::find(sib.begin(), sib.end(), static_cast<int>(i)) -
                                        sib.begin());
    if (at >= sib.size()) return d;  // a generated page is nobody's sibling
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
        next[i] = nav_title(render_extended(cfg, pages[i].src, vars_only, defaults, &hooks).vars,
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
      RenderedDoc r = render_extended(cfg, p.src, p.doc, defaults, &hooks);
      if (opt.extra_checks) {
        for (const auto& k : r.unknown_keys) c.warn_at(p.rel, "unknown template key {{" + k + "}}");
      }
      std::string md = std::move(r.markdown);
      // Templates usually leave blank lines at the top; they mean nothing to markdown.
      for (size_t nl; (nl = md.find('\n')) != std::string::npos && trim(md.substr(0, nl)).empty();) {
        md.erase(0, nl + 1);
      }
      if (!md.empty() && md.back() != '\n') md.push_back('\n');
      if (plugins.wants("transform_source")) {
        Json params = Json::make_object();
        params.set("page", Json(p.rel)).set("url", Json(p.url));
        md = plugins.transform("transform_source", params, "markdown", std::move(md));
      }
      if (!plugins.empty()) md = expand_containers(md, p.rel, plugins);

      // One parse serves the toc, the html and the text; only an expanded {{toc}} changes the
      // markdown and so needs a second.
      std::string toc_md;
      const bool has_toc = md.find(kTocSentinel) != std::string::npos;
      std::unique_ptr<MarkdownDoc> parsed;
      if (has_toc || skeleton.uses_toc) {
        parsed = std::make_unique<MarkdownDoc>(replace_all(md, kTocSentinel, ""), cfg.markdown);
        toc_md = toc_markdown(parsed->headings());
      }
      if (has_toc) {
        md = expand_toc(md, toc_md);
        parsed.reset();
      }
      if (!parsed) parsed = std::make_unique<MarkdownDoc>(md, cfg.markdown);
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

      c.prepare(dest);
      c.put(dest, md);
      const std::string body = parsed->html();
      const std::string toc_html = toc_md.empty() ? "" : markdown_to_html(toc_md, cfg.markdown);
      htmls[i] = render_skeleton(skeleton, r.vars, body, toc_html, hooks);
      if (plugins.wants("post_page")) {
        Json params = Json::make_object();
        params.set("page", Json(p.rel)).set("url", Json(p.url));
        htmls[i] = plugins.transform("post_page", params, "html", std::move(htmls[i]));
      }
      c.put(html_path, htmls[i]);
      c.put(txt_path, parsed->text());
      pages[i].vars = std::move(r.vars);
    } catch (const std::exception& e) {
      throw std::runtime_error(p.rel + ": " + e.what());
    }
    stats.page_list.push_back({p.url, p.rel, p.out_rel});
    if (opt.verbose) std::cerr << "ssg: built " << p.url << " from " << p.rel << "\n";
    ++stats.pages;
  }

  if (!cfg.base_url.empty()) {
    if (c.outputs.count("sitemap.xml")) {
      c.warn("sitemap.xml exists in the content dir; not generating one");
    } else {
      c.claim("sitemap.xml", "(sitemap)");
      c.put(cfg.dist_dir / "sitemap.xml", sitemap_xml(cfg, pages));
    }
  }

  if (cfg.feed.enabled) {
    if (cfg.base_url.empty()) {
      c.warn("[feed] needs base_url (entries carry absolute URLs); not writing feed.xml");
    } else if (c.outputs.count("feed.xml")) {
      c.warn("feed.xml exists in the content dir; not generating one");
    } else {
      const std::string prefix = cfg.feed.section.empty() ? "/" : "/" + cfg.feed.section + "/";
      std::vector<const Page*> dated;
      for (const auto& p : pages) {
        if (p.generated || !looks_like_date(var_or(p.vars, "date"))) continue;
        if (p.url == "/404/" || p.url == prefix || !starts_with(p.url, prefix)) continue;
        const auto it = p.vars.find("feed");
        if (it != p.vars.end() && !is_truthy(it->second)) continue;
        dated.push_back(&p);
      }
      std::sort(dated.begin(), dated.end(), [](const Page* a, const Page* b) {
        const auto da = var_or(a->vars, "date"), db = var_or(b->vars, "date");
        return da != db ? da > db : a->url < b->url;
      });
      if (dated.size() > static_cast<size_t>(cfg.feed.limit)) dated.resize(static_cast<size_t>(cfg.feed.limit));
      std::vector<FeedEntry> entries;
      for (const Page* p : dated) {
        entries.push_back({cfg.base_url + p->url,
                           nav_title(p->vars, p->rel),
                           var_or(p->vars, "date"), std::string(trim(var_or(p->vars, "description"))),
                           std::string(trim(var_or(p->vars, "author")))});
      }
      std::string title = cfg.feed.title;
      if (title.empty()) {
        title = cfg.base_url;
        if (const size_t at = title.find("://"); at != std::string::npos) title.erase(0, at + 3);
      }
      const std::string updated =
          entries.empty() ? today : entries.front().date.substr(0, 10);
      c.claim("feed.xml", "(feed)");
      c.put(cfg.dist_dir / "feed.xml",
                 feed_xml(title, cfg.feed.author, cfg.base_url + prefix, feed_url, updated, entries));
    }
  }

  if (cfg.robots.enabled) {
    if (c.outputs.count("robots.txt")) {
      // A default is not worth a warning on every build; a [robots] table that is ignored is.
      if (cfg.robots.table) c.warn("robots.txt exists in the content dir; not generating one");
    } else {
      c.claim("robots.txt", "(robots)");
      const bool sitemap = cfg.robots.sitemap && !cfg.base_url.empty() && c.outputs.count("sitemap.xml");
      c.put(cfg.dist_dir / "robots.txt",
                 robots_txt(cfg.robots, sitemap ? cfg.base_url + "/sitemap.xml" : ""));
    }
  }

  std::vector<Redirect> redirects;
  if (c.has_redirects) {
    std::vector<Diag> problems;
    redirects = parse_redirects(read_file(cfg.content_dir / "redirects.txt"), problems);
    for (const auto& d : problems) c.warn_at(d.file, d.message);
    if (!redirects.empty()) {
      if (c.outputs.count("_redirects")) {
        c.warn("_redirects exists in the content dir; not generating one from redirects.txt");
      } else {
        c.claim("_redirects", "(redirects.txt)");
        c.put(cfg.dist_dir / "_redirects", redirects_file(redirects));
      }
    }
    if (cfg.redirect_pages) {
      for (const Redirect& r : redirects) {
        const std::string stub = redirect_stub_path(r.from);
        if (stub.empty()) continue;  // a wildcard or odd path: only the host can redirect it
        if (c.outputs.count(stub)) {
          c.warn_at("redirects.txt", "line " + std::to_string(r.line) + ": " + stub +
                                         " already exists; no redirect page written");
          continue;
        }
        c.claim(stub, "(redirects.txt)");
        c.prepare(cfg.dist_dir / fs::path(stub));
        c.put(cfg.dist_dir / fs::path(stub), redirect_stub_html(r, cfg.base_url));
      }
    }
  }

  if (opt.deploy && !cfg.deploy_target.empty()) {
    DeployInput di;
    di.version = opt.version;
    for (const auto& kv : fingerprints) di.immutable.push_back(kv.second);
    std::sort(di.immutable.begin(), di.immutable.end());
    for (const DeployFile& f : deploy_files(cfg, di)) {
      if (f.in_dist) {
        if (c.outputs.count(f.path)) {
          c.warn(f.path + " exists in the content dir; not generating one for the " +
                 cfg.deploy_target + " target");
          continue;
        }
        c.claim(f.path, "(deploy " + cfg.deploy_target + ")");
        c.put(cfg.dist_dir / f.path, f.body);
        continue;
      }
      // A project file is the user's once written: only --force replaces one that differs.
      const fs::path dest = cfg.content_dir / f.path;
      if (fs::exists(dest)) {
        if (read_file(dest) == f.body) continue;
        if (!cfg.deploy_force) {
          c.warn(f.path + " exists and differs from what the " + cfg.deploy_target +
                 " target would generate; kept (pass --force to replace it)");
          continue;
        }
      }
      write_file(dest, f.body);
      if (!opt.quiet) std::cerr << "ssg: wrote " << f.path << " for " << cfg.deploy_target << "\n";
    }
  }

  fs::path manifest_file;  // where plugins can read this build's manifest; "" until it's written
  if (const fs::path mp = manifest_target(cfg); !mp.empty()) {
    if (path_is_inside(cfg.dist_dir, mp)) {
      throw std::runtime_error("manifest (" + mp.string() + ") is inside dist, which would deploy it");
    }
    if (fs::exists(mp) && !is_ssg_manifest(mp)) {
      c.warn("not writing " + mp.string() + ": it exists and ssg did not write it "
             "(set manifest = \"path\" or manifest = false in site.toml)");
    } else {
      fs::create_directories(mp.parent_path());
      write_file(mp, manifest_json(cfg, opt.version, today, pages, c.asset_rels, asset_sizes, redirects,
                                    fingerprints, inlined, plugins.active()));
      stats.manifest = mp.string();
      manifest_file = mp;
    }
  }
  // Plugins write their own files in post_build, so what is stale goes first.
  if (opt.incremental && cfg.clean && ours) c.prune();
  stats.changed = c.changed;
  stats.unchanged = c.unchanged;
  if (plugins.wants("post_build")) {
    if (manifest_file.empty()) {
      // No manifest on disk for this build (manifest = false); plugins still get one to read.
      manifest_file = scratch.make() / "manifest.json";
      write_file(manifest_file, manifest_json(cfg, opt.version, today, pages, c.asset_rels,
                                              asset_sizes, redirects, fingerprints, inlined,
                                              plugins.active()));
    }
    plugins.post_build(manifest_file, cfg.dist_dir);
  }
  plugins.stop();

  if (opt.check_links) {
    for (size_t i = 0; i < pages.size(); ++i) {
      std::set<std::string> seen;
      for (const auto& link : local_links(htmls[i])) {
        if (!seen.insert(link).second) continue;
        std::string target;
        if (!resolve_link(pages[i].url, link, target) || !link_target_exists(cfg.dist_dir, target)) {
          c.warn_at(pages[i].rel, "broken link " + link);
        }
      }
    }
  }

  if (opt.extra_checks) {
    // Targets some page links to, reduced to a page key: "a/b/", "a/b" and "a/b.html" all reduce to "a/b".
    std::set<std::string> linked;
    for (size_t i = 0; i < pages.size(); ++i) {
      for (const auto& link : local_links(htmls[i])) {
        std::string target;
        if (!resolve_link(pages[i].url, link, target)) continue;
        const std::string key = page_key("/" + target);
        if (key != page_key(pages[i].url)) linked.insert(key);
      }
      const size_t bad = imgs_without_alt(htmls[i]);
      if (bad) {
        c.warn_at(pages[i].rel, std::to_string(bad) + " image" + (bad == 1 ? "" : "s") +
                                    " without alt text");
      }
    }
    for (const auto& p : pages) {
      const std::string key = page_key(p.url);
      if (key.empty() || key == "404" || linked.count(key)) continue;
      c.warn_at(p.rel, "orphan page: nothing links to " + p.url);
    }
  }

  stats.warnings = c.warnings;
  stats.diags = c.diags;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count();
  stats.ms = ms;
  if (!opt.quiet) {
    std::cerr << "ssg: built " << stats.pages << " page" << (stats.pages == 1 ? "" : "s")
              << ", copied " << stats.files << " file" << (stats.files == 1 ? "" : "s") << " in "
              << ms << " ms";
    if (opt.incremental) std::cerr << ", " << c.changed << " written, " << c.unchanged << " unchanged";
    if (c.warnings) std::cerr << " (" << c.warnings << " warning" << (c.warnings == 1 ? "" : "s") << ")";
    std::cerr << "\n";
  }
  return stats;
}

bool clean_dist(const SiteConfig& cfg, std::string& why) {
  check_dist(cfg);
  if (!own_dist(cfg.dist_dir, why)) return false;
  if (!fs::exists(cfg.dist_dir)) return true;
  // remove_all doesn't follow symlinks, so a link in dist can't take anything else with it.
  for (const auto& e : fs::directory_iterator(cfg.dist_dir)) fs::remove_all(e.path());
  return true;
}

std::vector<Check> doctor_site(const SiteConfig& cfg) {
  std::vector<Check> out;
  auto add = [&](const char* status, const std::string& name, const std::string& msg) {
    out.push_back({status, name, msg});
  };
  add("ok", "config", cfg.config_path.string() + " parsed");
  for (const auto& d : cfg.warnings) add("warn", "config", d.file + ": " + d.message);
  try {
    check_dist(cfg);
    std::string why;
    if (own_dist(cfg.dist_dir, why)) {
      add("ok", "dist", cfg.dist_dir.string() + " is safe to build into");
    } else {
      add("warn", "dist", cfg.dist_dir.string() + " won't be cleaned: " + why);
    }
  } catch (const std::exception& e) {
    add("error", "dist", e.what());
  }
  // The marker is only a risk when the host publishes dist as is. A deploy file that names it
  // (a Cloudflare .assetsignore, a Vercel ignore file or config) is taken as the exclusion.
  if (fs::exists(cfg.dist_dir / kDistMarker)) {
    bool excluded = false;
    for (const fs::path& dir : {cfg.content_dir, cfg.dist_dir.parent_path(), cfg.dist_dir}) {
      for (const char* name : {".assetsignore", ".vercelignore", "vercel.json"}) {
        std::error_code ec;
        if (fs::is_regular_file(dir / name, ec) &&
            read_file(dir / name).find(kDistMarker) != std::string::npos) {
          excluded = true;
        }
      }
    }
    if (!excluded) {
      add("warn", "marker",
          (cfg.dist_dir / kDistMarker).string() +
              " would be deployed if the host publishes dist as is. Exclude it: a .assetsignore "
              "line for Cloudflare, a .vercelignore line for Vercel, a deny rule for nginx");
    }
  }
  if (!fs::is_directory(cfg.templates_dir)) {
    add("warn", "templates", "templates dir " + cfg.templates_dir.string() + " does not exist");
  } else {
    add("ok", "templates", cfg.templates_dir.string());
  }
  if (!fs::exists(cfg.skeleton_path)) {
    add("error", "skeleton", "missing HTML skeleton: " + cfg.skeleton_path.string());
  } else {
    try {
      load_skeleton(cfg);
      add("ok", "skeleton", cfg.skeleton_path.string() + " parses");
    } catch (const std::exception& e) {
      add("error", "skeleton", e.what());
    }
  }
  try {
    for (const PluginSpec& spec : plugin_specs(cfg)) {
      const std::string name = "plugin " + spec.name;
      if (!spec.enabled) {
        add("ok", name, "disabled (enabled = false)");
        continue;
      }
      const char* bad = spec.required ? "error" : "warn";
      std::string why;
      const fs::path exe = find_plugin(cfg, spec, why);
      if (exe.empty()) {
        add(bad, name, why);
        continue;
      }
      try {
        const PluginMeta meta = read_plugin_meta(exe, spec.timeout);
        if (meta.protocol > kPluginProtocol) {
          add(bad, name, exe.string() + " needs plugin protocol " + std::to_string(meta.protocol) +
                             ", but this ssg speaks " + std::to_string(kPluginProtocol));
          continue;
        }
        if (meta.name != spec.name) {
          add(bad, name, "--meta names the plugin '" + meta.name + "'");
          continue;
        }
        std::vector<std::string> notes;
        check_plugin_config(spec, meta, notes);
        for (const auto& n : notes) add("warn", name, n);
        add("ok", name, meta.version + " at " + exe.string());
      } catch (const std::exception& e) {
        add(bad, name, std::string(e.what()) + " (" + exe.string() + ")");
      }
    }
  } catch (const std::exception& e) {
    add("error", "plugins", e.what());
  }
  return out;
}
