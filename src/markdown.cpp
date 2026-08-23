#include "markdown.hpp"

#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct MdCtx {
  std::map<std::string, std::pair<std::string, std::string>> refs;
  std::map<std::string, std::string> footnotes;
  std::vector<std::string> footnote_order;
};

std::string html_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string norm_label(std::string_view s) {
  std::string out;
  bool space = false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

int indent_of(std::string_view line) {
  int n = 0;
  for (char c : line) {
    if (c == ' ') ++n;
    else if (c == '\t') n += 4 - (n % 4);
    else break;
  }
  return n;
}

std::string lstrip_indent(std::string_view line, int n) {
  int seen = 0;
  size_t i = 0;
  while (i < line.size() && seen < n) {
    if (line[i] == ' ') {
      ++seen;
      ++i;
    } else if (line[i] == '\t') {
      seen += 4 - (seen % 4);
      ++i;
    } else {
      break;
    }
  }
  return std::string(line.substr(i));
}

bool is_blank(std::string_view s) {
  for (char c : s) {
    if (c != ' ' && c != '\t') return false;
  }
  return true;
}

bool is_hr(std::string_view line) {
  if (line.find('|') != std::string_view::npos) return false;
  int n = 0;
  char kind = 0;
  for (char c : line) {
    if (c == ' ' || c == '\t') continue;
    if (c == '*' || c == '-' || c == '_') {
      if (!kind) kind = c;
      if (c != kind) return false;
      ++n;
    } else {
      return false;
    }
  }
  return n >= 3;
}

int heading_level(std::string_view line, std::string_view& text) {
  int n = 0;
  while (n < static_cast<int>(line.size()) && line[n] == '#') ++n;
  if (n == 0 || n > 6) return 0;
  if (n >= static_cast<int>(line.size()) || (line[n] != ' ' && line[n] != '\t')) return 0;
  size_t i = static_cast<size_t>(n);
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  text = line.substr(i);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '#')) {
    text.remove_suffix(1);
  }
  return n;
}

bool fence_open(std::string_view line, std::string& lang) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i + 3 > line.size() || line.substr(i, 3) != "```") return false;
  lang = std::string(line.substr(i + 3));
  while (!lang.empty() && (lang.front() == ' ' || lang.front() == '\t')) lang.erase(lang.begin());
  while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\t')) lang.pop_back();
  return true;
}

bool ul_item(std::string_view line, std::string_view& item) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i + 2 > line.size()) return false;
  if (!(line[i] == '-' || line[i] == '*' || line[i] == '+')) return false;
  if (line[i + 1] != ' ' && line[i + 1] != '\t') return false;
  item = line.substr(i + 2);
  return true;
}

bool ol_item(std::string_view line, std::string_view& item, int* start = nullptr) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i >= line.size() || !std::isdigit(static_cast<unsigned char>(line[i]))) return false;
  int n = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
    n = n * 10 + (line[i] - '0');
    ++i;
  }
  if (i >= line.size() || line[i] != '.') return false;
  ++i;
  if (i >= line.size() || (line[i] != ' ' && line[i] != '\t')) return false;
  item = line.substr(i + 1);
  if (start) *start = n;
  return true;
}

bool quote_line(std::string_view line, std::string_view& inner) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i >= line.size() || line[i] != '>') return false;
  ++i;
  if (i < line.size() && line[i] == ' ') ++i;
  inner = line.substr(i);
  return true;
}

bool def_start(std::string_view line, std::string_view& content) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i >= line.size()) return false;
  if (line[i] != ':' && line[i] != '~') return false;
  ++i;
  if (i >= line.size() || (line[i] != ' ' && line[i] != '\t')) return false;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  content = line.substr(i);
  return true;
}

int container_marker(std::string_view line, std::string& name) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3) return 0;
  int colons = 0;
  while (i < line.size() && line[i] == ':') {
    ++colons;
    ++i;
  }
  if (colons < 2) return 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  name = std::string(line.substr(i));
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
  if (name.empty()) return 2;
  for (char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return 0;
  }
  return 1;
}

bool parse_ref_def(std::string_view line, std::string& label, std::string& url, std::string& title) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i >= line.size() || line[i] != '[') return false;
  ++i;
  if (i < line.size() && line[i] == '^') return false;
  const size_t lab = i;
  while (i < line.size() && line[i] != ']') ++i;
  if (i >= line.size()) return false;
  label = norm_label(line.substr(lab, i - lab));
  ++i;
  if (i >= line.size() || line[i] != ':') return false;
  ++i;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i >= line.size()) return false;
  if (line[i] == '<') {
    ++i;
    const size_t u = i;
    while (i < line.size() && line[i] != '>') ++i;
    if (i >= line.size()) return false;
    url = std::string(line.substr(u, i - u));
    ++i;
  } else {
    const size_t u = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    url = std::string(line.substr(u, i - u));
  }
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  title.clear();
  if (i < line.size() && (line[i] == '"' || line[i] == '\'')) {
    const char q = line[i++];
    const size_t t = i;
    while (i < line.size() && line[i] != q) ++i;
    if (i >= line.size()) return false;
    title = std::string(line.substr(t, i - t));
  }
  return !label.empty() && !url.empty();
}

bool parse_footnote_def(std::string_view line, std::string& id, std::string& rest) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  if (i > 3 || i + 3 > line.size() || line[i] != '[' || line[i + 1] != '^') return false;
  i += 2;
  const size_t id_start = i;
  while (i < line.size() && line[i] != ']') ++i;
  if (i >= line.size() || i + 1 >= line.size() || line[i + 1] != ':') return false;
  id = std::string(line.substr(id_start, i - id_start));
  i += 2;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  rest = std::string(line.substr(i));
  return !id.empty();
}

std::vector<std::string> split_table_row(std::string_view line) {
  std::string_view t = line;
  while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.remove_prefix(1);
  while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.remove_suffix(1);
  if (!t.empty() && t.front() == '|') t.remove_prefix(1);
  if (!t.empty() && t.back() == '|') t.remove_suffix(1);
  std::vector<std::string> cells;
  std::string cur;
  for (char c : t) {
    if (c == '|') {
      while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
      size_t a = 0;
      while (a < cur.size() && (cur[a] == ' ' || cur[a] == '\t')) ++a;
      cells.push_back(cur.substr(a));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
  size_t a = 0;
  while (a < cur.size() && (cur[a] == ' ' || cur[a] == '\t')) ++a;
  cells.push_back(cur.substr(a));
  return cells;
}

bool is_table_sep(std::string_view line) {
  if (line.find('-') == std::string_view::npos) return false;
  auto cells = split_table_row(line);
  if (cells.empty()) return false;
  for (const auto& cell : cells) {
    size_t i = 0;
    while (i < cell.size() && (cell[i] == ' ' || cell[i] == '\t')) ++i;
    if (i < cell.size() && cell[i] == ':') ++i;
    int dashes = 0;
    while (i < cell.size() && cell[i] == '-') {
      ++dashes;
      ++i;
    }
    if (i < cell.size() && cell[i] == ':') ++i;
    while (i < cell.size() && (cell[i] == ' ' || cell[i] == '\t')) ++i;
    if (dashes < 3 || i != cell.size()) return false;
  }
  return true;
}

char table_align(std::string_view cell) {
  std::string t;
  for (char c : cell) {
    if (c != ' ' && c != '\t') t.push_back(c);
  }
  const bool left = !t.empty() && t.front() == ':';
  const bool right = !t.empty() && t.back() == ':';
  if (left && right) return 'c';
  if (right) return 'r';
  return 'l';
}

std::string align_attr(char a) {
  if (a == 'c') return " style=\"text-align:center\"";
  if (a == 'r') return " style=\"text-align:right\"";
  return {};
}

std::vector<std::string> split_lines(std::string_view md) {
  std::vector<std::string> lines;
  size_t i = 0;
  while (i <= md.size()) {
    size_t nl = md.find('\n', i);
    if (nl == std::string_view::npos) {
      lines.emplace_back(md.substr(i));
      break;
    }
    std::string line(md.substr(i, nl - i));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    i = nl + 1;
  }
  return lines;
}

std::string join_lines(const std::vector<std::string>& lines, size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to && i < lines.size(); ++i) {
    out += lines[i];
    out += '\n';
  }
  return out;
}

bool looks_like_table(const std::vector<std::string>& lines, size_t i) {
  return i + 1 < lines.size() && lines[i].find('|') != std::string::npos && is_table_sep(lines[i + 1]);
}

bool parse_dest_title(std::string_view s, size_t open_paren, std::string& url, std::string& title,
                      size_t& end) {
  size_t i = open_paren + 1;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  if (i >= s.size()) return false;
  if (s[i] == '<') {
    ++i;
    const size_t u = i;
    while (i < s.size() && s[i] != '>') ++i;
    if (i >= s.size()) return false;
    url = std::string(s.substr(u, i - u));
    ++i;
  } else {
    const size_t u = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != ')') ++i;
    url = std::string(s.substr(u, i - u));
  }
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  title.clear();
  if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
    const char q = s[i++];
    const size_t t = i;
    while (i < s.size() && s[i] != q) ++i;
    if (i >= s.size()) return false;
    title = std::string(s.substr(t, i - t));
    ++i;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  }
  if (i >= s.size() || s[i] != ')') return false;
  end = i;
  return !url.empty();
}

std::string inline_html(std::string_view s, MdCtx& ctx);
std::string markdown_to_html_inner(std::string_view md);

std::vector<std::string> extract_defs(const std::vector<std::string>& lines, MdCtx& ctx) {
  std::vector<std::string> body;
  for (size_t i = 0; i < lines.size();) {
    std::string lang;
    if (fence_open(lines[i], lang)) {
      body.push_back(lines[i++]);
      while (i < lines.size() && lines[i].find("```") == std::string::npos) body.push_back(lines[i++]);
      if (i < lines.size()) body.push_back(lines[i++]);
      continue;
    }
    if (indent_of(lines[i]) >= 4 && !is_blank(lines[i])) {
      while (i < lines.size() && (is_blank(lines[i]) || indent_of(lines[i]) >= 4)) {
        body.push_back(lines[i++]);
      }
      continue;
    }
    std::string fid, rest;
    if (parse_footnote_def(lines[i], fid, rest)) {
      std::string body_md = rest;
      ++i;
      while (i < lines.size()) {
        if (is_blank(lines[i])) {
          if (i + 1 < lines.size() && indent_of(lines[i + 1]) >= 4) {
            body_md += "\n";
            ++i;
            continue;
          }
          break;
        }
        if (indent_of(lines[i]) >= 4) {
          body_md += "\n" + lstrip_indent(lines[i], 4);
          ++i;
          continue;
        }
        break;
      }
      ctx.footnotes[fid] = body_md;
      continue;
    }
    std::string label, url, title;
    if (parse_ref_def(lines[i], label, url, title)) {
      ctx.refs[label] = {url, title};
      ++i;
      continue;
    }
    body.push_back(lines[i++]);
  }
  return body;
}

std::string footnote_ref_html(MdCtx& ctx, const std::string& id) {
  if (!ctx.footnotes.count(id)) return {};
  size_t n = 0;
  for (size_t i = 0; i < ctx.footnote_order.size(); ++i) {
    if (ctx.footnote_order[i] == id) {
      n = i + 1;
      break;
    }
  }
  if (n == 0) {
    ctx.footnote_order.push_back(id);
    n = ctx.footnote_order.size();
  }
  const std::string nid = html_escape(id);
  return "<sup class=\"footnote-ref\"><a href=\"#fn-" + nid + "\" id=\"fnref-" + nid + "\">" +
         std::to_string(n) + "</a></sup>";
}

std::string inline_html_from(std::string_view s, size_t& i, MdCtx& ctx) {
  if (i >= s.size()) return {};

  if (s[i] == '`') {
    size_t j = i + 1;
    while (j < s.size() && s[j] != '`') ++j;
    if (j < s.size()) {
      auto code = html_escape(s.substr(i + 1, j - i - 1));
      i = j + 1;
      return "<code>" + code + "</code>";
    }
  }

  if (s[i] == '^' && i + 1 < s.size() && s[i + 1] == '[') {
    size_t close = s.find(']', i + 2);
    if (close != std::string_view::npos) {
      std::string id = "inline-" + std::to_string(ctx.footnotes.size() + 1);
      ctx.footnotes[id] = std::string(s.substr(i + 2, close - (i + 2)));
      i = close + 1;
      return footnote_ref_html(ctx, id);
    }
  }

  if (s[i] == '[' && i + 1 < s.size() && s[i + 1] == '^') {
    size_t close = s.find(']', i + 2);
    if (close != std::string_view::npos) {
      std::string id(s.substr(i + 2, close - (i + 2)));
      auto html = footnote_ref_html(ctx, id);
      if (!html.empty()) {
        i = close + 1;
        return html;
      }
    }
  }

  auto ref_lookup = [&](std::string_view lab) -> const std::pair<std::string, std::string>* {
    auto it = ctx.refs.find(norm_label(lab));
    return it == ctx.refs.end() ? nullptr : &it->second;
  };

  if (s[i] == '!' && i + 1 < s.size() && s[i + 1] == '[') {
    size_t alt_end = s.find(']', i + 2);
    if (alt_end != std::string_view::npos) {
      auto alt = html_escape(s.substr(i + 2, alt_end - (i + 2)));
      if (alt_end + 1 < s.size() && s[alt_end + 1] == '(') {
        std::string url, title;
        size_t end = 0;
        if (parse_dest_title(s, alt_end + 1, url, title, end)) {
          i = end + 1;
          std::string tag = "<img src=\"" + html_escape(url) + "\" alt=\"" + alt + "\"";
          if (!title.empty()) tag += " title=\"" + html_escape(title) + "\"";
          return tag + ">";
        }
      } else {
        std::string_view lab = s.substr(i + 2, alt_end - (i + 2));
        size_t k = alt_end + 1;
        if (k < s.size() && s[k] == '[') {
          size_t lab_end = s.find(']', k + 1);
          if (lab_end != std::string_view::npos) {
            auto inner = s.substr(k + 1, lab_end - (k + 1));
            if (!inner.empty()) lab = inner;
            k = lab_end + 1;
          }
        }
        if (auto* ref = ref_lookup(lab)) {
          i = k;
          std::string tag = "<img src=\"" + html_escape(ref->first) + "\" alt=\"" + alt + "\"";
          if (!ref->second.empty()) tag += " title=\"" + html_escape(ref->second) + "\"";
          return tag + ">";
        }
      }
    }
  }

  if (s[i] == '[') {
    size_t text_end = s.find(']', i + 1);
    if (text_end != std::string_view::npos) {
      auto text = inline_html(s.substr(i + 1, text_end - (i + 1)), ctx);
      if (text_end + 1 < s.size() && s[text_end + 1] == '(') {
        std::string url, title;
        size_t end = 0;
        if (parse_dest_title(s, text_end + 1, url, title, end)) {
          i = end + 1;
          std::string tag = "<a href=\"" + html_escape(url) + "\"";
          if (!title.empty()) tag += " title=\"" + html_escape(title) + "\"";
          return tag + ">" + text + "</a>";
        }
      } else {
        std::string_view lab = s.substr(i + 1, text_end - (i + 1));
        size_t k = text_end + 1;
        if (k < s.size() && s[k] == '[') {
          size_t lab_end = s.find(']', k + 1);
          if (lab_end != std::string_view::npos) {
            auto inner = s.substr(k + 1, lab_end - (k + 1));
            if (!inner.empty()) lab = inner;
            k = lab_end + 1;
          }
        }
        if (auto* ref = ref_lookup(lab)) {
          i = k;
          std::string tag = "<a href=\"" + html_escape(ref->first) + "\"";
          if (!ref->second.empty()) tag += " title=\"" + html_escape(ref->second) + "\"";
          return tag + ">" + text + "</a>";
        }
      }
    }
  }

  auto take_wrap = [&](std::string_view mark, const char* open, const char* close) -> std::string {
    if (s.substr(i, mark.size()) != mark) return {};
    size_t k = s.find(mark, i + mark.size());
    if (k == std::string_view::npos) return {};
    auto inner = inline_html(s.substr(i + mark.size(), k - (i + mark.size())), ctx);
    i = k + mark.size();
    return std::string(open) + inner + close;
  };

  if (auto t = take_wrap("~~", "<del>", "</del>"); !t.empty()) return t;
  if (auto t = take_wrap("**", "<strong>", "</strong>"); !t.empty()) return t;
  if (auto t = take_wrap("__", "<strong>", "</strong>"); !t.empty()) return t;
  if (s[i] == '*' && (i + 1 >= s.size() || s[i + 1] != '*')) {
    if (auto t = take_wrap("*", "<em>", "</em>"); !t.empty()) return t;
  }
  if (s[i] == '_' && (i + 1 >= s.size() || s[i + 1] != '_')) {
    if (auto t = take_wrap("_", "<em>", "</em>"); !t.empty()) return t;
  }

  std::string one = html_escape(s.substr(i, 1));
  ++i;
  return one;
}

std::string inline_html(std::string_view s, MdCtx& ctx) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) out += inline_html_from(s, i, ctx);
  return out;
}

std::string inline_text(std::string_view s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s.substr(i, 2) == "~~" || s.substr(i, 2) == "**" || s.substr(i, 2) == "__") {
      i += 2;
      continue;
    }
    if (s[i] == '*' || s[i] == '_') {
      ++i;
      continue;
    }
    if (s[i] == '`') {
      size_t j = i + 1;
      while (j < s.size() && s[j] != '`') ++j;
      if (j < s.size()) {
        out.append(s.substr(i + 1, j - i - 1));
        i = j + 1;
        continue;
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

bool is_other_block(const std::vector<std::string>& lines, size_t at) {
  if (at >= lines.size() || is_blank(lines[at])) return true;
  std::string dummy;
  std::string_view tmp;
  int n = 0;
  if (container_marker(lines[at], dummy)) return true;
  return fence_open(lines[at], dummy) || is_hr(lines[at]) || heading_level(lines[at], tmp) ||
         quote_line(lines[at], tmp) || ul_item(lines[at], tmp) || ol_item(lines[at], tmp, &n) ||
         looks_like_table(lines, at) || indent_of(lines[at]) >= 4;
}

std::string render_blocks(const std::vector<std::string>& lines, MdCtx& ctx) {
  std::ostringstream out;
  size_t i = 0;
  std::vector<std::string> para;

  auto flush_para = [&]() {
    if (para.empty()) return;
    out << "<p>";
    for (size_t p = 0; p < para.size(); ++p) {
      if (p) out << " ";
      out << inline_html(para[p], ctx);
    }
    out << "</p>\n";
    para.clear();
  };

  while (i < lines.size()) {
    const auto& line = lines[i];
    if (is_blank(line)) {
      flush_para();
      ++i;
      continue;
    }

    std::string lang;
    if (fence_open(line, lang)) {
      flush_para();
      ++i;
      out << "<pre><code";
      if (!lang.empty()) out << " class=\"language-" << html_escape(lang) << "\"";
      out << ">";
      while (i < lines.size() && lines[i].find("```") == std::string::npos) {
        out << html_escape(lines[i]) << "\n";
        ++i;
      }
      if (i < lines.size()) ++i;
      out << "</code></pre>\n";
      continue;
    }

    if (indent_of(line) >= 4) {
      flush_para();
      out << "<pre><code>";
      while (i < lines.size() && (is_blank(lines[i]) || indent_of(lines[i]) >= 4)) {
        if (is_blank(lines[i])) {
          if (i + 1 >= lines.size() || indent_of(lines[i + 1]) < 4) break;
          out << "\n";
          ++i;
          continue;
        }
        out << html_escape(lstrip_indent(lines[i], 4)) << "\n";
        ++i;
      }
      out << "</code></pre>\n";
      continue;
    }

    std::string cname;
    if (container_marker(line, cname) == 1) {
      flush_para();
      ++i;
      const size_t start = i;
      std::string close_name;
      while (i < lines.size() && container_marker(lines[i], close_name) != 2) ++i;
      out << "<div class=\"alert " << html_escape(cname) << "\">\n";
      out << markdown_to_html_inner(join_lines(lines, start, i));
      out << "</div>\n";
      if (i < lines.size()) ++i;
      continue;
    }

    if (looks_like_table(lines, i)) {
      flush_para();
      auto header = split_table_row(lines[i]);
      auto seps = split_table_row(lines[i + 1]);
      std::vector<char> aligns(header.size(), 'l');
      for (size_t c = 0; c < header.size() && c < seps.size(); ++c) aligns[c] = table_align(seps[c]);
      i += 2;
      out << "<table>\n<thead>\n<tr>";
      for (size_t c = 0; c < header.size(); ++c) {
        out << "<th" << align_attr(aligns[c]) << ">" << inline_html(header[c], ctx) << "</th>";
      }
      out << "</tr>\n</thead>\n<tbody>\n";
      while (i < lines.size() && !is_blank(lines[i]) && lines[i].find('|') != std::string::npos &&
             !is_table_sep(lines[i])) {
        auto cells = split_table_row(lines[i]);
        out << "<tr>";
        for (size_t c = 0; c < header.size(); ++c) {
          std::string cell = c < cells.size() ? cells[c] : "";
          out << "<td" << align_attr(aligns[c]) << ">" << inline_html(cell, ctx) << "</td>";
        }
        out << "</tr>\n";
        ++i;
      }
      out << "</tbody>\n</table>\n";
      continue;
    }

    if (is_hr(line)) {
      flush_para();
      out << "<hr>\n";
      ++i;
      continue;
    }

    std::string_view heading;
    if (int lvl = heading_level(line, heading)) {
      flush_para();
      out << "<h" << lvl << ">" << inline_html(heading, ctx) << "</h" << lvl << ">\n";
      ++i;
      continue;
    }

    std::string_view qinner;
    if (quote_line(line, qinner)) {
      flush_para();
      std::string quoted;
      while (i < lines.size() && quote_line(lines[i], qinner)) {
        if (!quoted.empty()) quoted += "\n";
        quoted.append(qinner);
        ++i;
      }
      out << "<blockquote>\n" << markdown_to_html_inner(quoted) << "</blockquote>\n";
      continue;
    }

    std::string_view dcontent;
    const bool next_def =
        (i + 1 < lines.size() && def_start(lines[i + 1], dcontent)) ||
        (i + 2 < lines.size() && is_blank(lines[i + 1]) && def_start(lines[i + 2], dcontent));
    if (!is_other_block(lines, i) && next_def) {
      flush_para();
      out << "<dl>\n";
      while (i < lines.size()) {
        while (i < lines.size() && is_blank(lines[i])) ++i;
        std::vector<std::string> terms;
        while (i < lines.size() && !is_blank(lines[i]) && !def_start(lines[i], dcontent) &&
               !is_other_block(lines, i)) {
          terms.push_back(lines[i++]);
        }
        while (i < lines.size() && is_blank(lines[i])) ++i;
        if (terms.empty() || i >= lines.size() || !def_start(lines[i], dcontent)) break;
        for (const auto& term : terms) out << "<dt>" << inline_html(term, ctx) << "</dt>\n";
        while (i < lines.size() && def_start(lines[i], dcontent)) {
          std::string dd(dcontent);
          ++i;
          while (i < lines.size()) {
            if (is_blank(lines[i])) {
              if (i + 1 < lines.size() &&
                  (indent_of(lines[i + 1]) >= 4 || def_start(lines[i + 1], dcontent))) {
                dd += "\n";
                ++i;
                continue;
              }
              break;
            }
            if (def_start(lines[i], dcontent)) break;
            if (is_other_block(lines, i) && indent_of(lines[i]) < 4) break;
            dd += "\n";
            dd += indent_of(lines[i]) >= 4 ? lstrip_indent(lines[i], 4) : lines[i];
            ++i;
          }
          out << "<dd>\n" << markdown_to_html_inner(dd) << "</dd>\n";
        }
      }
      out << "</dl>\n";
      continue;
    }

    std::string_view item;
    if (ul_item(line, item)) {
      flush_para();
      out << "<ul>\n";
      while (i < lines.size() && ul_item(lines[i], item)) {
        out << "<li>" << inline_html(item, ctx) << "</li>\n";
        ++i;
      }
      out << "</ul>\n";
      continue;
    }
    int start_n = 1;
    if (ol_item(line, item, &start_n)) {
      flush_para();
      out << "<ol";
      if (start_n != 1) out << " start=\"" << start_n << "\"";
      out << ">\n";
      while (i < lines.size() && ol_item(lines[i], item)) {
        out << "<li>" << inline_html(item, ctx) << "</li>\n";
        ++i;
      }
      out << "</ol>\n";
      continue;
    }

    para.push_back(line);
    ++i;
  }
  flush_para();
  return out.str();
}

std::string render_footnotes(MdCtx& ctx) {
  if (ctx.footnote_order.empty()) return {};
  std::ostringstream out;
  out << "<section class=\"footnotes\">\n<ol>\n";
  for (const auto& id : ctx.footnote_order) {
    out << "<li id=\"fn-" << html_escape(id) << "\">\n";
    out << markdown_to_html_inner(ctx.footnotes[id]);
    out << " <a href=\"#fnref-" << html_escape(id) << "\" class=\"footnote-backref\">↩</a>\n";
    out << "</li>\n";
  }
  out << "</ol>\n</section>\n";
  return out.str();
}

std::string markdown_to_html_inner(std::string_view md) {
  MdCtx ctx;
  auto lines = extract_defs(split_lines(md), ctx);
  return render_blocks(lines, ctx) + render_footnotes(ctx);
}

}  // namespace

std::string markdown_to_html(std::string_view md) { return markdown_to_html_inner(md); }

std::string markdown_to_text(std::string_view md) {
  MdCtx ctx;
  auto lines = extract_defs(split_lines(md), ctx);
  std::ostringstream out;
  size_t i = 0;
  bool need_blank = false;

  auto emit = [&](const std::string& s) {
    if (s.empty()) return;
    if (need_blank) out << "\n";
    out << s;
    if (s.back() != '\n') out << "\n";
    need_blank = true;
  };

  while (i < lines.size()) {
    const auto& line = lines[i];
    if (is_blank(line)) {
      ++i;
      continue;
    }

    std::string lang, cname;
    std::string_view tmp;
    if (fence_open(line, lang)) {
      ++i;
      std::string code;
      while (i < lines.size() && lines[i].find("```") == std::string::npos) {
        code += lines[i];
        code += "\n";
        ++i;
      }
      if (i < lines.size()) ++i;
      emit(code);
      continue;
    }
    if (indent_of(line) >= 4) {
      std::string code;
      while (i < lines.size() && (is_blank(lines[i]) || indent_of(lines[i]) >= 4)) {
        if (is_blank(lines[i])) {
          if (i + 1 >= lines.size() || indent_of(lines[i + 1]) < 4) break;
          code += "\n";
          ++i;
          continue;
        }
        code += lstrip_indent(lines[i], 4);
        code += "\n";
        ++i;
      }
      emit(code);
      continue;
    }
    if (container_marker(line, cname) == 1) {
      ++i;
      const size_t start = i;
      std::string close_name;
      while (i < lines.size() && container_marker(lines[i], close_name) != 2) ++i;
      emit(markdown_to_text(join_lines(lines, start, i)));
      if (i < lines.size()) ++i;
      continue;
    }
    if (looks_like_table(lines, i)) {
      auto header = split_table_row(lines[i]);
      i += 2;
      std::string table;
      for (size_t c = 0; c < header.size(); ++c) {
        if (c) table += "\t";
        table += inline_text(header[c]);
      }
      table += "\n";
      while (i < lines.size() && !is_blank(lines[i]) && lines[i].find('|') != std::string::npos &&
             !is_table_sep(lines[i])) {
        auto cells = split_table_row(lines[i]);
        for (size_t c = 0; c < header.size(); ++c) {
          if (c) table += "\t";
          table += inline_text(c < cells.size() ? cells[c] : "");
        }
        table += "\n";
        ++i;
      }
      emit(table);
      continue;
    }
    if (is_hr(line)) {
      emit("----");
      ++i;
      continue;
    }
    if (heading_level(line, tmp)) {
      emit(inline_text(tmp));
      ++i;
      continue;
    }
    if (quote_line(line, tmp)) {
      std::string quoted;
      while (i < lines.size() && quote_line(lines[i], tmp)) {
        if (!quoted.empty()) quoted += "\n";
        quoted += "> ";
        quoted += inline_text(tmp);
        ++i;
      }
      emit(quoted);
      continue;
    }
    if (ul_item(line, tmp)) {
      std::string list;
      while (i < lines.size() && ul_item(lines[i], tmp)) {
        list += "- ";
        list += inline_text(tmp);
        list += "\n";
        ++i;
      }
      emit(list);
      continue;
    }
    if (ol_item(line, tmp)) {
      std::string list;
      int n = 1;
      while (i < lines.size() && ol_item(lines[i], tmp)) {
        list += std::to_string(n++) + ". ";
        list += inline_text(tmp);
        list += "\n";
        ++i;
      }
      emit(list);
      continue;
    }
    if (i + 1 < lines.size() && def_start(lines[i + 1], tmp)) {
      std::string dl;
      while (i < lines.size() && !is_blank(lines[i]) && !def_start(lines[i], tmp)) {
        dl += inline_text(lines[i]);
        dl += "\n";
        ++i;
      }
      while (i < lines.size() && def_start(lines[i], tmp)) {
        dl += ": ";
        dl += inline_text(tmp);
        dl += "\n";
        ++i;
      }
      emit(dl);
      continue;
    }

    std::string p;
    while (i < lines.size() && !is_blank(lines[i])) {
      std::string dummy;
      std::string_view t2;
      if (fence_open(lines[i], dummy) || is_hr(lines[i]) || heading_level(lines[i], t2) ||
          quote_line(lines[i], t2) || ul_item(lines[i], t2) || ol_item(lines[i], t2) ||
          looks_like_table(lines, i) || container_marker(lines[i], dummy) ||
          indent_of(lines[i]) >= 4) {
        break;
      }
      if (!p.empty()) p += " ";
      p += inline_text(lines[i]);
      ++i;
    }
    emit(p);
  }

  if (!ctx.footnotes.empty()) {
    std::string fn;
    int n = 1;
    auto order = ctx.footnote_order;
    if (order.empty()) {
      for (const auto& kv : ctx.footnotes) order.push_back(kv.first);
    }
    for (const auto& id : order) fn += "[" + std::to_string(n++) + "] " + inline_text(ctx.footnotes[id]) + "\n";
    emit(fn);
  }
  return out.str();
}
