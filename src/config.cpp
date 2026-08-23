#include "config.hpp"
#include "util.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

static std::string unquote(std::string_view raw) {
  raw = trim(raw);
  if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
    throw std::runtime_error("site.toml values must be quoted strings");
  }
  return std::string(raw.substr(1, raw.size() - 2));
}

SiteConfig load_site_config(const std::filesystem::path& content_dir) {
  const auto toml_path = content_dir / "site.toml";
  if (!std::filesystem::exists(toml_path)) {
    throw std::runtime_error("missing site.toml in " + content_dir.string());
  }

  SiteConfig cfg;
  cfg.content_dir = std::filesystem::absolute(content_dir);

  std::string dist, templates, skeleton;
  const std::string text = read_file(toml_path);
  std::string_view in = text;
  size_t line_no = 0;

  while (!in.empty()) {
    size_t nl = in.find('\n');
    std::string_view line = nl == std::string_view::npos ? in : in.substr(0, nl);
    in = nl == std::string_view::npos ? std::string_view{} : in.substr(nl + 1);
    ++line_no;

    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    line = trim(line);
    if (line.empty() || line.front() == '#') continue;

    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      throw std::runtime_error("site.toml:" + std::to_string(line_no) + ": expected key = \"value\"");
    }

    const auto key = trim(line.substr(0, eq));
    const auto value = unquote(line.substr(eq + 1));

    if (key == "dist") dist = value;
    else if (key == "templates") templates = value;
    else if (key == "skeleton") skeleton = value;
  }

  if (dist.empty() || templates.empty() || skeleton.empty()) {
    throw std::runtime_error("site.toml must set dist, templates, and skeleton");
  }

  auto resolve = [&](const std::string& p) {
    std::filesystem::path path(p);
    if (path.is_relative()) path = cfg.content_dir / path;
    return path.lexically_normal();
  };

  cfg.dist_dir = resolve(dist);
  cfg.templates_dir = resolve(templates);
  cfg.skeleton_path = resolve(skeleton);
  return cfg;
}
