#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

inline std::string read_file(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    throw std::runtime_error("cannot read " + path.string() + ": is a directory");
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read file: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) throw std::runtime_error("error reading file: " + path.string());
  return ss.str();
}

// Replaces whatever is at `path`; a symlink there is removed, never written through.
inline void write_file(const std::filesystem::path& path, std::string_view data) {
  std::filesystem::create_directories(path.parent_path());
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) {
    std::filesystem::remove(path);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot write file: " + path.string());
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  out.flush();
  out.close();
  if (!out) throw std::runtime_error("error writing file (disk full?): " + path.string());
}

inline bool starts_with(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

inline bool ends_with(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

inline std::string_view ltrim(std::string_view s) {
  auto i = s.find_first_not_of(" \t\r\n");
  return i == std::string_view::npos ? std::string_view{} : s.substr(i);
}

inline std::string_view rtrim(std::string_view s) {
  auto i = s.find_last_not_of(" \t\r\n");
  return i == std::string_view::npos ? std::string_view{} : s.substr(0, i + 1);
}

inline std::string_view trim(std::string_view s) { return rtrim(ltrim(s)); }

inline std::string html_escape(std::string_view s) {
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

// html_escape plus ', for attribute values in either quote style.
inline std::string html_escape_attr(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\'') out += "&#39;";
    else out += html_escape(std::string_view(&c, 1));
  }
  return out;
}

inline std::string replace_all(std::string_view s, std::string_view from, std::string_view to) {
  if (from.empty()) return std::string(s);
  std::string out;
  size_t i = 0;
  for (size_t at; (at = s.find(from, i)) != std::string_view::npos; i = at + from.size()) {
    out.append(s.substr(i, at - i));
    out.append(to);
  }
  out.append(s.substr(i));
  return out;
}

// Relative path with forward slashes, the form ignore patterns and URLs use.
inline std::string generic_rel(const std::filesystem::path& p, const std::filesystem::path& base) {
  return p.lexically_relative(base).generic_string();
}

inline bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

inline std::string replace_ci_tag_inner(const std::string& html,
                                        std::string_view open_name,
                                        std::string_view close_name,
                                        const std::string& inner,
                                        bool required = true,
                                        bool pad_newlines = true) {
  auto find_ci = [](const std::string& hay, std::string_view needle, size_t from) -> size_t {
    for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
      if (iequals(std::string_view(hay.data() + i, needle.size()), needle)) return i;
    }
    return std::string::npos;
  };

  const size_t open_at = find_ci(html, open_name, 0);
  if (open_at == std::string::npos) {
    if (!required) return html;
    throw std::runtime_error("HTML skeleton has no " + std::string(open_name) + " tag");
  }
  const size_t gt = html.find('>', open_at);
  if (gt == std::string::npos) {
    if (!required) return html;
    throw std::runtime_error("HTML skeleton has a malformed " + std::string(open_name) + " tag");
  }
  const size_t close_at = find_ci(html, close_name, gt + 1);
  if (close_at == std::string::npos) {
    if (!required) return html;
    throw std::runtime_error("HTML skeleton has no " + std::string(close_name) + " tag");
  }

  std::string out;
  out.reserve(html.size() + inner.size() + 2);
  out.append(html, 0, gt + 1);
  if (pad_newlines) {
    out.push_back('\n');
    out.append(inner);
    if (!inner.empty() && inner.back() != '\n') out.push_back('\n');
  } else {
    out.append(inner);
  }
  out.append(html, close_at, std::string::npos);
  return out;
}
