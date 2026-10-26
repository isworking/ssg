#include "ignore.hpp"
#include "util.hpp"

namespace {

// git's ctype: ASCII only, whatever the locale.
bool in_class(std::string_view name, unsigned char c, bool& known) {
  const bool upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z';
  const bool digit = c >= '0' && c <= '9';
  const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
  const bool print = c >= 0x20 && c <= 0x7e;
  known = true;
  if (name == "alnum") return upper || lower || digit;
  if (name == "alpha") return upper || lower;
  if (name == "blank") return c == ' ' || c == '\t';
  if (name == "cntrl") return c < 0x20 || c == 0x7f;
  if (name == "digit") return digit;
  if (name == "graph") return print && !space;
  if (name == "lower") return lower;
  if (name == "print") return print;
  if (name == "punct") return print && !space && !upper && !lower && !digit;
  if (name == "space") return space;
  if (name == "upper") return upper;
  if (name == "xdigit") return digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  known = false;
  return false;
}

std::string_view basename_of(std::string_view rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string_view::npos ? rel : rel.substr(slash + 1);
}

}  // namespace

// Mirrors git's dowild() pattern walk, so malformed patterns fail the same way (never match).
Glob::Glob(std::string_view p) {
  const size_t n = p.size();
  for (size_t i = 0; i < n && !never_;) {
    const char c = p[i];
    if (c == '\\') {
      if (i + 1 >= n) never_ = true;
      else toks_.push_back({Kind::Lit, p[i + 1], 0});
      i += 2;
    } else if (c == '?') {
      toks_.push_back({Kind::Any, 0, 0});
      ++i;
    } else if (c == '*') {
      size_t k = i;
      while (k < n && p[k] == '*') ++k;
      Kind kind = Kind::Star;
      // "**" alone between slashes (or the pattern's ends) crosses directories.
      if (k - i >= 2 && (i == 0 || p[i - 1] == '/') &&
          (k == n || p[k] == '/' || (p[k] == '\\' && k + 1 < n && p[k + 1] == '/'))) {
        kind = k < n && p[k] == '/' ? Kind::StarDirs : Kind::StarSlash;
      }
      toks_.push_back({kind, 0, 0});
      i = k;
    } else if (c == '[') {
      std::bitset<256> set;
      size_t j = i + 1;
      bool neg = false;
      if (j < n && (p[j] == '!' || p[j] == '^')) {
        neg = true;
        ++j;
      }
      int prev = -1;  // last single char, a range start
      for (bool first = true;; first = false, ++j) {
        if (j >= n) {
          never_ = true;
          break;
        }
        const auto pc = static_cast<unsigned char>(p[j]);
        if (pc == ']' && !first) break;
        if (pc == '\\') {
          if (++j >= n) {
            never_ = true;
            break;
          }
          prev = static_cast<unsigned char>(p[j]);
          set.set(static_cast<size_t>(prev));
        } else if (pc == '-' && prev >= 0 && j + 1 < n && p[j + 1] != ']') {
          auto hi = static_cast<unsigned char>(p[++j]);
          if (hi == '\\') {
            if (++j >= n) {
              never_ = true;
              break;
            }
            hi = static_cast<unsigned char>(p[j]);
          }
          for (int x = prev; x <= hi; ++x) set.set(static_cast<size_t>(x));
          prev = -1;
        } else if (pc == '[' && j + 1 < n && p[j + 1] == ':') {
          size_t k = j + 2;
          while (k < n && p[k] != ']') ++k;
          if (k >= n) {
            never_ = true;
            break;
          }
          if (k == j + 2 || p[k - 1] != ':') {  // no ":]": a plain '['
            set.set('[');
            prev = '[';
            continue;
          }
          const auto name = p.substr(j + 2, k - j - 3);
          bool known = false;
          for (int x = 0; x < 256; ++x) {
            if (in_class(name, static_cast<unsigned char>(x), known)) set.set(static_cast<size_t>(x));
            if (!known) break;
          }
          if (!known) {
            never_ = true;
            break;
          }
          prev = -1;
          j = k;
        } else {
          set.set(pc);
          prev = pc;
        }
      }
      if (neg) set.flip();
      toks_.push_back({Kind::Set, 0, sets_.size()});
      sets_.push_back(set);
      i = j + 1;
    } else {
      toks_.push_back({Kind::Lit, c, 0});
      ++i;
    }
  }
}

// One left-to-right pass per token over the set of reachable path offsets.
bool Glob::match(std::string_view s) const {
  if (never_) return false;
  const size_t n = s.size();
  std::vector<char> cur(n + 1, 0), nxt(n + 1), carry;
  cur[0] = 1;
  for (size_t ti = 0; ti < toks_.size(); ++ti) {
    const Tok& t = toks_[ti];
    std::fill(nxt.begin(), nxt.end(), 0);
    bool any = false;
    if (t.kind == Kind::Star || t.kind == Kind::StarSlash || t.kind == Kind::StarDirs) {
      const bool slash = t.kind != Kind::Star;
      bool alive = false;
      for (size_t k = 0; k <= n; ++k) {
        alive = cur[k] || (alive && (slash || s[k - 1] != '/'));
        nxt[k] = alive;
        any = any || alive;
      }
      if (t.kind == Kind::StarDirs) carry = cur;  // "**/" may also match nothing at all
    } else {
      for (size_t k = 0; k < n; ++k) {
        if (!cur[k]) continue;
        const auto c = static_cast<unsigned char>(s[k]);
        const bool ok = t.kind == Kind::Lit ? s[k] == t.c
                        : t.kind == Kind::Any ? c != '/'
                                              : c != '/' && sets_[t.set].test(c);
        if (ok) nxt[k + 1] = any = true;
      }
    }
    if (ti > 0 && toks_[ti - 1].kind == Kind::StarDirs) {
      for (size_t k = 0; k <= n; ++k) {
        if (carry[k]) nxt[k] = any = true;
      }
    }
    if (!any) return false;
    cur.swap(nxt);
  }
  return cur[n];
}

bool glob_match(std::string_view pattern, std::string_view path) {
  return Glob(pattern).match(path);
}

// Follows git's add_patterns_from_buffer / parse_path_pattern.
void IgnoreRules::add(std::string_view base, std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line.empty() || line.front() == '#') return;
  // Trailing spaces go unless escaped ("foo\ " keeps one; "foo\\ " keeps none).
  size_t keep = line.size();
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == ' ') {
      if (keep == line.size()) keep = i;
      continue;
    }
    keep = line.size();
    if (line[i] == '\\' && ++i >= line.size()) break;  // a trailing '\' keeps every space
  }
  line = line.substr(0, keep);

  Rule r;
  r.base = std::string(base);
  if (!line.empty() && line.front() == '!') {
    r.negate = true;
    line.remove_prefix(1);
  }
  // "\!" and "\#" stay as written: the matcher reads them as literal characters.
  if (!line.empty() && line.back() == '/') {
    r.dir_only = true;
    line.remove_suffix(1);
  }
  r.anchored = line.find('/') != std::string_view::npos;
  if (r.anchored && line.front() == '/') line.remove_prefix(1);
  if (line.empty()) return;
  r.glob = Glob(line);
  rules_.push_back(std::move(r));
}

void IgnoreRules::add_file(std::string_view base, std::string_view text) {
  if (starts_with(text, "\xEF\xBB\xBF")) text.remove_prefix(3);
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    add(base, text.substr(0, nl));
    if (nl == std::string_view::npos) break;
    text.remove_prefix(nl + 1);
  }
}

bool IgnoreRules::ignored(std::string_view rel, bool is_dir) const {
  // Later rules win; deeper files are added after their ancestors, so they win too.
  bool out = false;
  for (const auto& r : rules_) {
    if (r.dir_only && !is_dir) continue;
    std::string_view sub = rel;
    if (!r.base.empty()) {
      if (sub.size() <= r.base.size() || !starts_with(sub, r.base) || sub[r.base.size()] != '/') {
        continue;
      }
      sub.remove_prefix(r.base.size() + 1);
    }
    if (r.glob.match(r.anchored ? sub : basename_of(sub))) out = !r.negate;
  }
  return out;
}
