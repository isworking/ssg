// ssg-plugin-highlight: post_page. Wraps comments, strings, numbers and keywords in the code
// blocks of known languages in <span class="hl-...">. It is a lexer, not a parser: good enough
// to read, never wrong about what the text says.
#include "plugin_main.hpp"
#include "util.hpp"

#include <map>
#include <set>

namespace {

const char* kMeta =
    "{\"name\":\"highlight\",\"version\":\"1.0.0\",\"protocol\":1,\"pure\":true,"
    "\"description\":\"Highlights fenced code blocks with <span class=hl-...> wrappers\","
    "\"hooks\":[\"init\",\"post_page\"],\"config\":{"
    "\"prefix\":{\"type\":\"string\",\"default\":\"hl-\"}}}";

struct Lang {
  std::vector<std::string> line_comments;
  bool block_comments = false;  // /* ... */
  std::string quotes;           // string delimiters
  std::set<std::string> keywords;
};

std::set<std::string> words(const std::string& s) {
  std::set<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    const size_t e = s.find(' ', i);
    out.insert(s.substr(i, e == std::string::npos ? std::string::npos : e - i));
    if (e == std::string::npos) break;
    i = e + 1;
  }
  return out;
}

const std::map<std::string, Lang>& languages() {
  static const std::map<std::string, Lang> table = [] {
    std::map<std::string, Lang> t;
    const Lang c{{"//"}, true, "\"'",
                 words("auto break case char const continue default do double else enum extern "
                       "float for goto if inline int long register return short signed sizeof "
                       "static struct switch typedef union unsigned void volatile while class "
                       "namespace template typename public private protected virtual new delete "
                       "this using bool true false nullptr throw try catch constexpr override")};
    for (const char* n : {"c", "h", "cpp", "c++", "cc", "hpp", "cxx"}) t[n] = c;
    const Lang js{{"//"}, true, "\"'`",
                  words("async await break case catch class const continue default delete do "
                        "else export extends false finally for from function if import in "
                        "instanceof let new null of return super switch this throw true try "
                        "typeof undefined var void while yield interface type enum implements "
                        "readonly")};
    for (const char* n : {"js", "javascript", "ts", "typescript", "jsx", "tsx", "mjs"}) t[n] = js;
    t["go"] = {{"//"}, true, "\"'`",
               words("break case chan const continue default defer else fallthrough for func go "
                     "goto if import interface map package range return select struct switch "
                     "type var true false nil")};
    const Lang rust{{"//"}, true, "\"'",
                    words("as async await break const continue crate else enum extern false fn "
                          "for if impl in let loop match mod move mut pub ref return self Self "
                          "static struct super trait true type unsafe use where while dyn")};
    for (const char* n : {"rust", "rs"}) t[n] = rust;
    const Lang py{{"#"}, false, "\"'",
                  words("and as assert async await break class continue def del elif else except "
                        "finally for from global if import in is lambda None nonlocal not or "
                        "pass raise return try while with yield True False")};
    for (const char* n : {"python", "py"}) t[n] = py;
    const Lang sh{{"#"}, false, "\"'",
                  words("if then else elif fi for while until do done case esac in function "
                        "return exit local export echo cd set unset source")};
    for (const char* n : {"sh", "bash", "shell", "zsh", "console"}) t[n] = sh;
    t["toml"] = {{"#"}, false, "\"'", words("true false")};
    const Lang yaml{{"#"}, false, "\"'", words("true false null yes no")};
    for (const char* n : {"yaml", "yml"}) t[n] = yaml;
    t["json"] = {{}, false, "\"", words("true false null")};
    t["css"] = {{}, true, "\"'", words("important inherit initial none auto var calc")};
    return t;
  }();
  return table;
}

std::string unescape(std::string_view s) {
  static const std::pair<std::string_view, char> ents[] = {
      {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&amp;", '&'}};
  std::string out;
  for (size_t i = 0; i < s.size();) {
    bool done = false;
    for (const auto& e : ents) {
      if (s.substr(i, e.first.size()) == e.first) {
        out += e.second;
        i += e.first.size();
        done = true;
        break;
      }
    }
    if (!done) out += s[i++];
  }
  return out;
}

std::string escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string lex(const std::string& code, const Lang& lang, const std::string& prefix) {
  std::string out;
  auto span = [&](const char* cls, std::string_view text) {
    out += "<span class=\"" + prefix + cls + "\">" + escape(text) + "</span>";
  };
  size_t i = 0;
  while (i < code.size()) {
    const char c = code[i];
    bool matched = false;
    for (const auto& lc : lang.line_comments) {
      if (code.compare(i, lc.size(), lc) == 0) {
        size_t e = code.find('\n', i);
        if (e == std::string::npos) e = code.size();
        span("comment", std::string_view(code).substr(i, e - i));
        i = e;
        matched = true;
        break;
      }
    }
    if (matched) continue;
    if (lang.block_comments && code.compare(i, 2, "/*") == 0) {
      size_t e = code.find("*/", i + 2);
      e = e == std::string::npos ? code.size() : e + 2;
      span("comment", std::string_view(code).substr(i, e - i));
      i = e;
      continue;
    }
    if (lang.quotes.find(c) != std::string::npos) {
      size_t e = i + 1;
      while (e < code.size() && code[e] != c && (code[e] != '\n' || c == '`')) {
        e += code[e] == '\\' && e + 1 < code.size() ? 2 : 1;
      }
      e = std::min(e, code.size());
      if (e < code.size() && code[e] == c) ++e;
      span("string", std::string_view(code).substr(i, e - i));
      i = e;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      size_t e = i;
      while (e < code.size() && (std::isalnum(static_cast<unsigned char>(code[e])) || code[e] == '.' ||
                                 code[e] == '_')) {
        ++e;
      }
      span("number", std::string_view(code).substr(i, e - i));
      i = e;
      continue;
    }
    if (ident_start(c)) {
      size_t e = i + 1;
      while (e < code.size() && ident_char(code[e])) ++e;
      const std::string w = code.substr(i, e - i);
      if (lang.keywords.count(w)) span("keyword", w);
      else out += escape(w);
      i = e;
      continue;
    }
    out += escape(std::string_view(code).substr(i, 1));
    ++i;
  }
  return out;
}

Json handle(const std::string& hook, const Json& params, PluginContext& ctx) {
  if (hook == "init") return Json("ok");
  if (hook != "post_page") throw std::runtime_error("unknown hook " + hook);
  const std::string html = params.str_at("html");
  const std::string prefix = ctx.config.str_at("prefix", "hl-");
  static const std::string open = "<pre><code class=\"language-";
  static const std::string close = "</code></pre>";
  std::string out;
  size_t pos = 0;
  while (true) {
    const size_t at = html.find(open, pos);
    if (at == std::string::npos) break;
    const size_t name_start = at + open.size();
    const size_t quote = html.find('"', name_start);
    if (quote == std::string::npos || html.compare(quote, 2, "\">") != 0) break;
    const size_t body = quote + 2;
    const size_t end = html.find(close, body);
    if (end == std::string::npos) break;
    out.append(html, pos, body - pos);
    const auto lang = languages().find(html.substr(name_start, quote - name_start));
    const std::string_view inner = std::string_view(html).substr(body, end - body);
    if (lang == languages().end() || inner.find("<span") != std::string_view::npos) {
      out.append(inner);
    } else {
      out += lex(unescape(inner), lang->second, prefix);
    }
    pos = end;
  }
  out.append(html, pos, std::string::npos);
  return Json(out);
}

}  // namespace

int main(int argc, char** argv) { return plugin_main(argc, argv, kMeta, handle); }
