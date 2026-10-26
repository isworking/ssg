#include "document.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace {

// Stand-in for an escaped "{{" (two plane-16 private-use codepoints). Contains no '{', so no
// later pass can see a tag in it; render_extended/interpolate turn it back into "{{" at the end.
constexpr std::string_view kOpenSentinel = "\xF4\x8F\xBF\xBD\xF4\x8F\xBF\xBC";
constexpr size_t kMaxDepth = 64;         // nested key/include expansions
constexpr size_t kMaxIfNesting = 256;
constexpr size_t kMaxOutput = size_t{64} << 20;

using Vars = std::map<std::string, std::string>;

bool is_key_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
}

bool is_blank(char c) { return c == ' ' || c == '\t'; }

size_t skip_blanks(std::string_view s, size_t i) {
  while (i < s.size() && is_blank(s[i])) ++i;
  return i;
}

size_t scan_key(std::string_view s, size_t i) {
  while (i < s.size() && is_key_char(s[i])) ++i;
  return i;
}

enum class Tok { Text, Key, Extend, Include, If, IfNot, Else, End };

struct Token {
  Tok kind = Tok::Text;
  std::string text;  // literal text, key name or path
  std::string raw;   // the tag as written
};

bool is_control(Tok k) { return k == Tok::If || k == Tok::IfNot || k == Tok::Else || k == Tok::End; }

// s[i..] starts with "{{". Returns the index past the closing "}}", or 0 if it isn't a tag.
size_t parse_tag(std::string_view s, size_t i, Token& tok) {
  auto close = [&](size_t k) -> size_t {
    k = skip_blanks(s, k);
    return k + 1 < s.size() && s[k] == '}' && s[k + 1] == '}' ? k + 2 : 0;
  };
  const size_t j = skip_blanks(s, i + 2);

  const std::pair<std::string_view, Tok> directives[] = {{"extend", Tok::Extend},
                                                          {"include", Tok::Include}};
  for (const auto& [word, kind] : directives) {
    if (s.substr(j, word.size()) != word) continue;
    const size_t k = skip_blanks(s, j + word.size());
    if (k >= s.size() || s[k] != '"') continue;
    size_t q = k + 1;
    while (q < s.size() && s[q] != '"' && s[q] != '\n') ++q;
    if (q >= s.size() || s[q] != '"') continue;
    if (const size_t end = close(q + 1)) {
      tok.kind = kind;
      tok.text = std::string(s.substr(k + 1, q - k - 1));
      return end;
    }
  }

  const size_t word_end = scan_key(s, j);
  if (word_end == j) return 0;
  const std::string_view word = s.substr(j, word_end - j);
  if (const size_t end = close(word_end)) {
    tok.kind = word == "else" ? Tok::Else : word == "end" ? Tok::End : Tok::Key;
    if (tok.kind == Tok::Key) tok.text = std::string(word);
    return end;
  }
  if (word != "if") return 0;

  // {{if key}} / {{if not key}}
  auto name_at = [&](size_t from, size_t& name_end) -> std::string_view {
    const size_t k = skip_blanks(s, from);
    if (k == from) return {};
    name_end = scan_key(s, k);
    return s.substr(k, name_end - k);
  };
  size_t e1 = 0;
  const auto name = name_at(word_end, e1);
  if (name.empty()) return 0;
  if (const size_t end = close(e1)) {
    tok.kind = Tok::If;
    tok.text = std::string(name);
    return end;
  }
  if (name != "not") return 0;
  size_t e2 = 0;
  const auto negated = name_at(e1, e2);
  if (negated.empty()) return 0;
  if (const size_t end = close(e2)) {
    tok.kind = Tok::IfNot;
    tok.text = std::string(negated);
    return end;
  }
  return 0;
}

// `standalone`: an if/else/end tag alone on its line takes the whole line (and its newline) with it.
std::vector<Token> tokenize(std::string_view s, bool standalone) {
  std::vector<Token> out;
  std::string lit;
  auto flush = [&]() {
    if (!lit.empty()) {
      out.push_back({Tok::Text, std::move(lit), {}});
      lit.clear();
    }
  };

  for (size_t i = 0; i < s.size();) {
    if (s[i] == '\\' && i + 2 < s.size() && s[i + 1] == '{' && s[i + 2] == '{') {
      lit += kOpenSentinel;
      i += 3;
      continue;
    }
    if (!(s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{')) {
      lit.push_back(s[i]);
      ++i;
      continue;
    }
    Token tok;
    size_t end = parse_tag(s, i, tok);
    if (!end) {
      lit.push_back(s[i]);
      ++i;
      continue;
    }
    tok.raw = std::string(s.substr(i, end - i));

    if (standalone && is_control(tok.kind)) {
      size_t ls = i;
      while (ls > 0 && is_blank(s[ls - 1])) --ls;
      size_t le = skip_blanks(s, end);
      const bool bol = ls == 0 || s[ls - 1] == '\n' || s[ls - 1] == '\r';
      if (bol && (le == s.size() || s[le] == '\n' || s[le] == '\r')) {
        lit.resize(lit.size() - (i - ls));  // the blanks before the tag are the tail of lit
        if (le < s.size()) le += s.compare(le, 2, "\r\n") == 0 ? 2 : 1;
        end = le;
      }
    }
    flush();
    out.push_back(std::move(tok));
    i = end;
  }
  flush();
  return out;
}

void unescape(std::string& s) {
  for (size_t at = s.find(kOpenSentinel); at != std::string::npos;
       at = s.find(kOpenSentinel, at + 2)) {
    s.replace(at, kOpenSentinel.size(), "{{");
  }
}

std::filesystem::path template_path(const SiteConfig& cfg, const std::string& file,
                                    const std::string& from) {
  std::filesystem::path p(file);
  bool bad = file.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory();
  for (const auto& part : p) {
    if (part == "..") bad = true;
  }
  if (bad) {
    throw std::runtime_error("bad template path \"" + file + "\" in " + from +
                             " (must be relative to the templates dir, without \"..\")");
  }
  if (p.extension() != ".md") p += ".md";
  return cfg.templates_dir / p;
}

ParsedDoc parse_template_file(const std::filesystem::path& path) {
  const std::string src = read_file(path);
  try {
    return parse_markdown_source(src);
  } catch (const std::runtime_error& e) {
    throw std::runtime_error(path.string() + ": " + e.what());
  }
}

struct Ctx {
  const SiteConfig* cfg = nullptr;  // null: {{include}} stays as written
  std::string source;               // for error messages
  bool final_pass = false;          // false: only keys are filled, other tags kept as written
  std::vector<std::string> keys;    // keys being expanded, outermost first
  std::vector<uint64_t> frames;     // a unique id per entry of `keys`
  uint64_t next_frame = 0;
  // Shallowest index into `keys` that the current expansion left as written (a cycle), or npos.
  size_t low = std::string::npos;
  std::vector<std::string> includes;
  size_t depth = 0;
};

class Evaluator {
 public:
  Evaluator(const Vars& vars, Ctx& ctx) : vars_(vars), ctx_(ctx) {}

  std::string eval(std::string_view s) {
    const auto toks = tokenize(s, ctx_.final_pass);
    std::string out;
    size_t pos = 0;
    run(toks, pos, out, true, 0);
    if (pos < toks.size()) {
      fail(toks[pos].kind == Tok::Else ? "{{else}} without {{if}}" : "{{end}} without {{if}}");
    }
    return out;
  }

 private:
  struct DepthGuard {
    Ctx& ctx;
    explicit DepthGuard(Ctx& c) : ctx(c) {
      if (++ctx.depth > kMaxDepth) {
        throw std::runtime_error("template expansion nested more than " +
                                 std::to_string(kMaxDepth) + " levels deep in " + ctx.source);
      }
    }
    ~DepthGuard() { --ctx.depth; }
  };

  [[noreturn]] void fail(const std::string& what) const {
    std::string where = ctx_.source;
    if (!ctx_.includes.empty()) where += " (in include " + ctx_.includes.back() + ")";
    throw std::runtime_error(what + " in " + where);
  }

  // Appends toks from pos on (when `emit`) and stops at an else/end belonging to the caller.
  void run(const std::vector<Token>& toks, size_t& pos, std::string& out, bool emit,
           size_t nesting) {
    while (pos < toks.size()) {
      const Token& t = toks[pos];
      if (ctx_.final_pass && (t.kind == Tok::Else || t.kind == Tok::End)) return;
      ++pos;

      if (ctx_.final_pass && (t.kind == Tok::If || t.kind == Tok::IfNot)) {
        if (nesting >= kMaxIfNesting) fail("{{if}} nested too deeply");
        const bool cond = emit && truthy(t.text) == (t.kind == Tok::If);
        run(toks, pos, out, emit && cond, nesting + 1);
        if (pos < toks.size() && toks[pos].kind == Tok::Else) {
          ++pos;
          run(toks, pos, out, emit && !cond, nesting + 1);
          if (pos < toks.size() && toks[pos].kind == Tok::Else) fail("second {{else}} in one {{if}}");
        }
        if (pos >= toks.size()) fail("missing {{end}} for " + t.raw);
        ++pos;
        continue;
      }
      if (!emit) continue;

      switch (t.kind) {
        case Tok::Text: out += t.text; break;
        case Tok::Key: {
          std::string v;
          if (expand_key(t.text, v)) {
            out += v;
          } else {
            out += "{{" + t.text + "}}";
          }
          break;
        }
        case Tok::Extend: out += "{{extend \"" + t.text + "\"}}"; break;
        case Tok::Include:
          if (ctx_.final_pass && ctx_.cfg) {
            out += include(t.text);
          } else {
            out += t.raw;
          }
          break;
        default: out += t.raw; break;
      }
      if (out.size() > kMaxOutput) fail("template expansion larger than 64 MiB");
    }
  }

  bool truthy(const std::string& key) {
    std::string v;
    return expand_key(key, v) && is_truthy(v);
  }

  // False when the key is undefined or already being expanded (it then stays as written).
  //
  // A result that hit no cycle doesn't depend on the stack and is memoised for good. One that did
  // is reused until the outermost key's expansion ends, so within it every key is expanded at
  // most once (cycles are cut the way a depth-first walk cuts them). That keeps cyclic keys mixed
  // with {{if}} quadratic in the number of keys; exact per-stack results would be exponential.
  bool expand_key(const std::string& key, std::string& out) {
    constexpr size_t npos = std::string::npos;
    const auto it = vars_.find(key);
    if (it == vars_.end()) return false;
    auto& keys = ctx_.keys;
    if (const auto on = std::find(keys.begin(), keys.end(), key); on != keys.end()) {
      ctx_.low = std::min(ctx_.low, static_cast<size_t>(on - keys.begin()));
      return false;
    }
    if (recall(memo_, key, out)) return true;
    DepthGuard guard(ctx_);
    const size_t d = keys.size();
    const size_t outer_low = ctx_.low;
    ctx_.low = npos;
    keys.push_back(key);
    ctx_.frames.push_back(++ctx_.next_frame);
    try {
      out = eval(it->second);
    } catch (...) {
      keys.pop_back();
      ctx_.frames.pop_back();
      throw;
    }
    keys.pop_back();
    ctx_.frames.pop_back();
    remember(memo_, key, out, d, outer_low);
    return true;
  }

  struct Memo {
    enum Scope { kAlways, kTop, kRoot };  // hit no cycle; outermost key; under the same one
    std::string value;
    Scope scope;
    uint64_t frame;  // ctx_.frames[0] when it was made (kRoot)
    size_t low;      // what to merge into ctx_.low on reuse
  };
  using Memos = std::unordered_map<std::string, Memo>;

  bool recall(const Memos& memo, const std::string& id, std::string& out) {
    const auto m = memo.find(id);
    if (m == memo.end()) return false;
    const Memo& e = m->second;
    const bool live = e.scope == Memo::kAlways ? true
                      : e.scope == Memo::kTop  ? ctx_.keys.empty()
                                               : !ctx_.keys.empty() && ctx_.frames[0] == e.frame;
    if (!live) return false;
    out = e.value;
    ctx_.low = std::min(ctx_.low, e.low);
    return true;
  }

  // After evaluating `id` at stack depth `d` (ctx_.low then holds the hits inside it).
  void remember(Memos& memo, const std::string& id, const std::string& out, size_t d,
                size_t outer_low) {
    constexpr size_t npos = std::string::npos;
    const size_t low = ctx_.low < d ? ctx_.low : npos;  // a hit on the key itself stays inside
    Memo e{out, Memo::kAlways, 0, low};
    if (ctx_.low != npos) {
      e.scope = d == 0 ? Memo::kTop : Memo::kRoot;
      e.frame = ctx_.frames.empty() ? 0 : ctx_.frames[0];
      if (low != npos) e.low = 0;  // the outermost key is the one frame sure to still be there
    }
    memo[id] = std::move(e);
    ctx_.low = std::min(outer_low, low);
  }

  std::string include(const std::string& name) {
    const auto path = template_path(*ctx_.cfg, name, ctx_.source);
    const std::string id = path.lexically_normal().string();
    if (std::find(ctx_.includes.begin(), ctx_.includes.end(), id) != ctx_.includes.end()) {
      fail("cyclic {{include \"" + name + "\"}}");
    }
    std::string out;
    if (recall(include_memo_, id, out)) return out;  // same rules as keys
    if (!std::filesystem::exists(path)) {
      fail("missing template \"" + name + "\" (looked for " + path.string() + ")");
    }
    DepthGuard guard(ctx_);
    ParsedDoc part = parse_template_file(path);
    if (!part.extend_file.empty()) fail("{{extend}} inside included template " + path.string());
    std::string& body = part.body;
    if (!body.empty() && body.back() == '\n') body.pop_back();
    if (!body.empty() && body.back() == '\r') body.pop_back();

    const size_t outer_low = ctx_.low;
    ctx_.low = std::string::npos;
    ctx_.includes.push_back(id);
    if (part.vars.empty()) {
      out = eval(body);
    } else {
      // The partial's own keys are defaults for it; the including page's keys win.
      Vars merged = std::move(part.vars);
      for (const auto& kv : vars_) merged[kv.first] = kv.second;
      out = Evaluator(merged, ctx_).eval(body);
    }
    ctx_.includes.pop_back();
    remember(include_memo_, id, out, ctx_.keys.size(), outer_low);
    return out;
  }

  const Vars& vars_;
  Ctx& ctx_;
  Memos memo_;
  Memos include_memo_;  // by normalised path
};

// After a closing quote only blanks or a # comment may follow.
bool only_comment(std::string_view s) {
  s = ltrim(s);
  return s.empty() || s.front() == '#';
}

// Everything between the first and the last quote, verbatim: no escapes, so "$a \neq b$" and
// "C:\new" survive. With `comment_ok`, a " # comment" may follow the last quote.
bool parse_double_quoted(std::string_view rest, bool comment_ok, std::string& value) {
  size_t close = rest.size() - 1;
  if (rest.size() < 2 || rest.back() != '"') {
    close = comment_ok ? rest.rfind('"') : 0;
    if (close == 0 || close == std::string_view::npos || !only_comment(rest.substr(close + 1))) {
      return false;
    }
  }
  value = std::string(rest.substr(1, close - 1));
  return true;
}

bool parse_single_quoted(std::string_view rest, std::string& value) {
  std::string v;
  for (size_t i = 1; i < rest.size(); ++i) {
    if (rest[i] != '\'') {
      v.push_back(rest[i]);
    } else if (i + 1 < rest.size() && rest[i + 1] == '\'') {
      v.push_back('\'');
      ++i;
    } else {
      if (!only_comment(rest.substr(i + 1))) return false;
      value = std::move(v);
      return true;
    }
  }
  return false;
}

// key: "double" | 'single' | bare (only when `bare_ok`; a " # comment" tail is dropped).
bool parse_frontmatter_line(std::string_view line, bool bare_ok, std::string& key,
                            std::string& value) {
  line = trim(line);
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos) return false;
  const auto k = trim(line.substr(0, colon));
  if (k.empty()) return false;
  for (char c : k) {
    if (!is_key_char(c)) return false;
  }
  auto rest = trim(line.substr(colon + 1));
  bool ok = false;
  if (!rest.empty() && rest.front() == '"') {
    ok = parse_double_quoted(rest, bare_ok, value);
  } else if (bare_ok && !rest.empty() && rest.front() == '\'') {
    ok = parse_single_quoted(rest, value);
  } else if (bare_ok) {
    for (size_t i = 1; i < rest.size(); ++i) {
      if (rest[i] == '#' && is_blank(rest[i - 1])) {
        rest = rest.substr(0, i);
        break;
      }
    }
    value = std::string(trim(rest));
    ok = true;
  }
  if (ok) key = std::string(k);
  return ok;
}

// Lines end at "\n", "\r\n" or a lone "\r", as in the markdown engine.
bool take_line(std::string_view& rest, std::string_view& line) {
  if (rest.empty()) return false;
  const size_t eol = rest.find_first_of("\r\n");
  line = rest.substr(0, eol);
  if (eol == std::string_view::npos) {
    rest = {};
  } else {
    rest.remove_prefix(eol + (rest.compare(eol, 2, "\r\n") == 0 ? 2 : 1));
  }
  return true;
}

bool is_fence(std::string_view line) { return trim(line) == "---"; }

}  // namespace

std::vector<Segment> parse_segments(std::string_view s) {
  std::vector<Segment> out;
  for (auto& t : tokenize(s, false)) {
    switch (t.kind) {
      case Tok::Text: out.push_back({false, std::move(t.text)}); break;
      case Tok::Key: out.push_back({true, std::move(t.text)}); break;
      case Tok::Extend: out.push_back({true, "extend:" + t.text}); break;
      case Tok::Include: out.push_back({true, "include:" + t.text}); break;
      case Tok::If: out.push_back({true, "if:" + t.text}); break;
      case Tok::IfNot: out.push_back({true, "if-not:" + t.text}); break;
      case Tok::Else: out.push_back({true, "else:"}); break;
      case Tok::End: out.push_back({true, "end:"}); break;
    }
  }
  return out;
}

bool is_truthy(std::string_view v) {
  v = trim(v);
  return !v.empty() && !iequals(v, "false") && !iequals(v, "0") && !iequals(v, "no") &&
         !iequals(v, "off");
}

ParsedDoc parse_markdown_source(std::string_view source) {
  ParsedDoc doc;
  if (starts_with(source, "\xEF\xBB\xBF")) source.remove_prefix(3);
  std::string_view rest = source;
  std::string_view first;
  std::string_view peek = rest;
  const bool fenced = take_line(peek, first) && is_fence(first);

  if (fenced) {
    take_line(rest, first);
    bool closed = false;
    std::string_view line;
    for (size_t lineno = 2; take_line(rest, line); ++lineno) {
      if (is_fence(line)) {
        closed = true;
        break;
      }
      const auto t = trim(line);
      if (t.empty() || t.front() == '#') continue;
      std::string key, value;
      if (!parse_frontmatter_line(line, true, key, value)) {
        throw std::runtime_error("frontmatter line " + std::to_string(lineno) +
                                 ": expected key: value, got: " + std::string(t));
      }
      doc.vars[key] = std::move(value);
    }
    if (!closed) throw std::runtime_error("unclosed frontmatter (missing closing ---)");
  } else {
    // Unfenced: leading `key: "value"` lines only (double quotes required, no comments).
    while (!rest.empty()) {
      std::string_view saved = rest;
      std::string_view line;
      take_line(rest, line);
      if (trim(line).empty()) break;
      std::string key, value;
      if (!parse_frontmatter_line(line, false, key, value)) {
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

std::string interpolate(std::string_view s, const std::map<std::string, std::string>& vars) {
  Ctx ctx;
  ctx.source = "template text";
  std::string out = Evaluator(vars, ctx).eval(s);
  unescape(out);
  return out;
}

RenderedDoc render_extended(const SiteConfig& cfg, const std::filesystem::path& source_path,
                            const ParsedDoc& doc,
                            const std::map<std::string, std::string>& defaults) {
  Vars vars = doc.vars;
  std::string body = doc.body;
  std::string extend = doc.extend_file;
  std::set<std::string> seen;
  Ctx ctx;
  ctx.cfg = &cfg;
  ctx.source = source_path.string();

  while (!extend.empty()) {
    if (!seen.insert(extend).second) {
      throw std::runtime_error("cyclic {{extend}} in " + source_path.string());
    }
    const auto path = template_path(cfg, extend, ctx.source);
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error("missing template \"" + extend + "\" (looked for " + path.string() +
                               ")");
    }
    ParsedDoc tmpl = parse_template_file(path);
    for (const auto& kv : tmpl.vars) {
      if (!vars.count(kv.first)) vars[kv.first] = kv.second;
    }
    vars["content"] = body;
    body = Evaluator(vars, ctx).eval(tmpl.body);
    extend = tmpl.extend_file;
  }
  for (const auto& kv : defaults) vars.emplace(kv.first, kv.second);

  // Final pass: keys, conditionals and includes, with every template merged.
  ctx.final_pass = true;
  RenderedDoc out;
  {
    Evaluator ev(vars, ctx);
    for (const auto& kv : vars) out.vars[kv.first] = ev.eval(kv.second);
  }
  out.markdown = Evaluator(out.vars, ctx).eval(body);

  unescape(out.markdown);
  for (auto& kv : out.vars) unescape(kv.second);
  return out;
}
