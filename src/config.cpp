#include "config.hpp"
#include "deploy.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {


bool is_known_table(const std::vector<std::string>& t) {
  if (t.empty()) return true;
  if (t[0] == "markdown" || t[0] == "robots" || t[0] == "feed" ||
      t[0] == "css" || t[0] == "deploy" || t[0] == "headers") return t.size() == 1;
  if (t[0] == "taxonomy") return t.size() <= 2;
  return t[0] == "plugin";
}

}  // namespace

SiteConfig load_site_config(const std::filesystem::path& content_dir,
                            const std::filesystem::path& config_file) {
  const auto toml_path = config_file.empty() ? content_dir / "site.toml" : config_file;
  if (!std::filesystem::exists(toml_path)) {
    throw std::runtime_error(config_file.empty()
                                 ? "missing site.toml in " + content_dir.string()
                                 : "missing config file " + config_file.string());
  }
  const std::string toml_name = toml_path.filename().string();

  SiteConfig cfg;
  auto warn = [&](int line, const std::string& msg) {
    std::cerr << "ssg: " << warning_label() << " " << toml_name << ":" << line << ": " << msg
              << "\n";
    cfg.warnings.push_back({toml_name + ":" + std::to_string(line), msg});
  };
  cfg.config_path = std::filesystem::absolute(toml_path).lexically_normal();
  cfg.content_dir = std::filesystem::absolute(content_dir).lexically_normal();
  if (!cfg.content_dir.has_filename() && cfg.content_dir.has_relative_path()) {
    cfg.content_dir = cfg.content_dir.parent_path();  // "dir/" -> "dir"
  }

  std::string dist, templates, skeleton, manifest;
  std::string text = read_file(toml_path);
  if (starts_with(text, "\xEF\xBB\xBF")) text.erase(0, 3);  // UTF-8 BOM (Windows editors)
  const TomlDoc doc = parse_toml(text, toml_name);

  // [taxonomy.<name>] tables, created on first mention.
  std::map<std::string, TaxonomyConfig> taxonomies;
  auto taxonomy = [&](const std::string& name) -> TaxonomyConfig& {
    bool ok = !name.empty();
    for (char ch : name) {
      ok = ok && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-');
    }
    if (!ok) {
      throw std::runtime_error(toml_name + ": taxonomy name '" + name +
                               "' may hold only letters, digits, - and _");
    }
    TaxonomyConfig& t = taxonomies[name];
    t.name = name;
    return t;
  };

  // Unknown tables are warned about once; nothing under them is looked at.
  std::set<std::vector<std::string>> unknown;
  auto under_unknown = [&](const std::vector<std::string>& path, size_t len) {
    for (size_t n = 1; n <= len; ++n) {
      if (unknown.count(std::vector<std::string>(path.begin(), path.begin() + n))) return true;
    }
    return false;
  };
  // Whether a table under t was already warned about, so [a.b] then [a] warns once, like
  // [a] then [a.b] does.
  auto above_unknown = [&](const std::vector<std::string>& t) {
    for (const std::vector<std::string>& u : unknown) {
      if (u.size() > t.size() && std::equal(t.begin(), t.end(), u.begin())) return true;
    }
    return false;
  };
  auto check_table = [&](const std::vector<std::string>& t, int line) {
    if (is_known_table(t) || under_unknown(t, t.size()) || above_unknown(t)) return;
    // A dotted key like foo.bar = 1 names table [foo]; a header names itself.
    unknown.insert(t);
    warn(line, "unknown table [" + toml_path_string(t) + "]");
  };
  auto plugin_seen = [&](const std::string& name) {
    if (!cfg.plugins.count(name)) cfg.plugin_order.push_back(name);
    return &cfg.plugins[name];
  };
  auto visit_table = [&](const TomlTable& t) {
    check_table(t.path, t.line);
    if (t.path.size() == 2 && t.path[0] == "taxonomy") taxonomy(t.path[1]);
    if (t.path[0] == "feed") cfg.feed.enabled = true;
    if (t.path.size() >= 2 && t.path[0] == "plugin") plugin_seen(t.path[1]);
  };

  // Headers and entries are each in file order; walk them together so warnings come out in order.
  size_t ti = 0;
  for (const TomlEntry& e : doc.entries) {
    for (; ti < doc.tables.size() && doc.tables[ti].line < e.line; ++ti) {
      visit_table(doc.tables[ti]);
    }
    const int line = e.line;
    const TomlValue& v = e.value;
    const std::vector<std::string>& path = e.path;
    // Keys are named relative to [markdown], as they're written there.
    const size_t skip = path[0] == "markdown" && path.size() > 1 ? 1 : 0;
    const std::string key =
        toml_path_string(std::vector<std::string>(path.begin() + skip, path.end()));
    auto err = [&](const std::string& msg) {
      throw std::runtime_error(toml_name + ":" + std::to_string(line) + ": " + msg);
    };
    auto want = [&](TomlValue::Kind k) {
      if (v.kind != k) err("'" + key + "' must be " + toml_kind_name(k));
    };

    if (path[0] == "plugin") {
      if (path.size() == 1) err("'plugin' must be a table, like [plugin.<name>]");
      if (path.size() == 2) {
        err("[plugin] holds one table per plugin, like [plugin." + toml_path_string({path[1]}) + "]");
      }
      plugin_seen(path[1])->push_back(
          {std::vector<std::string>(path.begin() + 2, path.end()), v, line});
    } else if (path.size() == 1) {
      const std::string& k = path[0];
      if (k == "dist") { want(TomlValue::String); dist = v.str; }
      else if (k == "templates") { want(TomlValue::String); templates = v.str; }
      else if (k == "skeleton") { want(TomlValue::String); skeleton = v.str; }
      else if (k == "base_url") { want(TomlValue::String); cfg.base_url = v.str; }
      else if (k == "manifest") {
        if (v.kind == TomlValue::Bool) {
          cfg.write_manifest = v.boolean;
        } else {
          want(TomlValue::String);
          if (v.str.empty()) err("'manifest' must be false or a file path");
          manifest = v.str;
        }
      }
      else if (k == "clean") { want(TomlValue::Bool); cfg.clean = v.boolean; }
      else if (k == "redirect_pages") { want(TomlValue::Bool); cfg.redirect_pages = v.boolean; }
      else if (k == "future") { want(TomlValue::Bool); cfg.future = v.boolean; }
      else if (k == "nested_repos") { want(TomlValue::Bool); cfg.nested_repos = v.boolean; }
      else if (k == "follow_symlinks") { want(TomlValue::Bool); cfg.follow_symlinks = v.boolean; }
      else if (k == "ignore") {
        want(TomlValue::Array);
        cfg.ignore.clear();
        for (const TomlValue& item : v.array) {
          if (item.kind != TomlValue::String) err("'ignore' must be an array of strings");
          cfg.ignore.push_back(item.str);
        }
      }
      else warn(line, "unknown key '" + key + "'");
    } else if (path[0] == "taxonomy") {
      if (path.size() == 1) err("'taxonomy' must be a table, like [taxonomy.tags]");
      if (path.size() == 2) err("'taxonomy." + toml_path_string({path[1]}) + "' must be a table");
      TaxonomyConfig& t = taxonomy(path[1]);
      if (path.size() > 3) {
        warn(line, "unknown key '" + key + "' in [taxonomy." + path[1] + "]");
      } else if (path[2] == "key") {
        want(TomlValue::String);
        t.key = v.str;
        if (t.key.empty()) err("'" + key + "' must not be empty");
      } else if (path[2] == "path") {
        want(TomlValue::String);
        std::string pth = v.str;
        while (!pth.empty() && pth.front() == '/') pth.erase(0, 1);
        while (!pth.empty() && pth.back() == '/') pth.pop_back();
        bool ok = !pth.empty() && pth.find('\\') == std::string::npos;
        for (size_t at = 0; ok && at <= pth.size();) {
          size_t end = pth.find('/', at);
          if (end == std::string::npos) end = pth.size();
          const std::string part = pth.substr(at, end - at);
          ok = !part.empty() && part != "." && part != "..";
          at = end + 1;
        }
        if (!ok) err("'" + key + "' must be a relative path like tags or topics/tags");
        t.path = pth;
      } else if (path[2] == "title") {
        want(TomlValue::String);
        t.title = v.str;
      } else {
        warn(line, "unknown key '" + key + "' in [taxonomy." + path[1] + "]");
      }
    } else if (path[0] == "feed") {
      cfg.feed.enabled = true;
      const std::string k = path.size() == 2 ? path[1] : "";
      if (k == "section") {
        want(TomlValue::String);
        std::string sec = v.str;
        while (!sec.empty() && sec.front() == '/') sec.erase(0, 1);
        while (!sec.empty() && sec.back() == '/') sec.pop_back();
        cfg.feed.section = sec;
      }
      else if (k == "title") { want(TomlValue::String); cfg.feed.title = v.str; }
      else if (k == "author") { want(TomlValue::String); cfg.feed.author = v.str; }
      else if (k == "limit") {
        want(TomlValue::Int);
        if (v.integer < 1 || v.integer > 10000) err("'feed.limit' must be between 1 and 10000");
        cfg.feed.limit = static_cast<int>(v.integer);
      } else if (!under_unknown(path, path.size() - 1)) {
        warn(line, "unknown key '" + key + "' in [feed]");
      }
    } else if (path[0] == "robots") {
      cfg.robots.table = true;
      const std::string k = path.size() == 2 ? path[1] : "";
      if (k == "enabled") { want(TomlValue::Bool); cfg.robots.enabled = v.boolean; }
      else if (k == "sitemap") { want(TomlValue::Bool); cfg.robots.sitemap = v.boolean; }
      else if (k == "disallow") {
        want(TomlValue::Array);
        cfg.robots.disallow.clear();
        for (const TomlValue& item : v.array) {
          if (item.kind != TomlValue::String || item.str.empty() || item.str[0] != '/') {
            err("'robots.disallow' must be an array of paths starting with /");
          }
          cfg.robots.disallow.push_back(item.str);
        }
      } else if (!under_unknown(path, path.size() - 1)) {
        warn(line, "unknown key '" + key + "' in [robots]");
      }
    } else if (path[0] == "css") {
      const std::string k = path.size() == 2 ? path[1] : "";
      if (k == "minify") { want(TomlValue::Bool); cfg.css.minify = v.boolean; }
      else if (k == "inline_limit") {
        want(TomlValue::Int);
        if (v.integer < 0 || v.integer > (1 << 30)) err("'css.inline_limit' must be 0 or more bytes");
        cfg.css.inline_limit = static_cast<size_t>(v.integer);
      } else if (!under_unknown(path, path.size() - 1)) {
        warn(line, "unknown key '" + key + "' in [css]");
      }
    } else if (path[0] == "deploy") {
      const std::string k = path.size() == 2 ? path[1] : "";
      if (k == "target") {
        want(TomlValue::String);
        if (!v.str.empty() && !known_deploy_target(v.str)) {
          err("'deploy.target' must be cloudflare, vercel or nginx, not '" + v.str + "'");
        }
        cfg.deploy_target = v.str;
      } else if (!under_unknown(path, path.size() - 1)) {
        warn(line, "unknown key '" + key + "' in [deploy]");
      }
    } else if (path[0] == "headers") {
      const std::string k = path.size() == 2 ? path[1] : "";
      if (k == "link") { want(TomlValue::Bool); cfg.headers.link = v.boolean; }
      else if (k == "format") { want(TomlValue::Bool); cfg.headers.format = v.boolean; }
      else if (k == "version") { want(TomlValue::Bool); cfg.headers.version = v.boolean; }
      else if (k == "page_cache") {
        want(TomlValue::String);
        if (v.str.find_first_of("\r\n\"") != std::string::npos) {
          err("'headers.page_cache' must be a single line without quotes");
        }
        cfg.headers.page_cache = v.str;
      } else if (!under_unknown(path, path.size() - 1)) {
        warn(line, "unknown key '" + key + "' in [headers]");
      }
    } else if (path[0] == "markdown" && path.size() == 2) {
      const std::string& k = path[1];
      auto& m = cfg.markdown;
      if (k == "html") { want(TomlValue::Bool); m.raw_html = v.boolean; }
      else if (k == "scripts") { want(TomlValue::Bool); m.scripts = v.boolean; }
      else if (k == "allow_tags" || k == "allow_attrs") {
        want(TomlValue::Array);
        auto& list = k == "allow_tags" ? m.allow_tags : m.allow_attrs;
        list.clear();
        for (const TomlValue& item : v.array) {
          if (item.kind != TomlValue::String) err("'" + k + "' must be an array of strings");
          std::string name = item.str;
          for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          bool ok = !name.empty();
          for (char c : name) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) ok = false;
          }
          if (!ok) err("'" + k + "' entry '" + item.str + "' is not a plain name");
          list.push_back(name);
        }
      }
      else if (k == "heading_ids") { want(TomlValue::Bool); m.heading_ids = v.boolean; }
      else if (k == "autolinks") { want(TomlValue::Bool); m.autolinks = v.boolean; }
      else if (k == "extensions") { want(TomlValue::Bool); m.extensions = v.boolean; }
      else if (k == "text_width") {
        want(TomlValue::Int);
        if (v.integer < 0 || v.integer > 100000) err("'text_width' must be between 0 and 100000");
        m.text_width = static_cast<int>(v.integer);
      } else {
        warn(line, "unknown key '" + key + "' in [markdown]");
      }
    } else if (path[0] == "markdown") {
      if (!under_unknown(path, path.size() - 1)) {
        warn(line, "unknown key '" + key + "' in [markdown]");
      }
    } else if (!under_unknown(path, path.size() - 1)) {
      check_table({path[0]}, line);
    }
  }
  for (; ti < doc.tables.size(); ++ti) visit_table(doc.tables[ti]);

  if (dist.empty() || templates.empty() || skeleton.empty()) {
    throw std::runtime_error(toml_name + " must set dist, templates, and skeleton");
  }
  while (!cfg.base_url.empty() && cfg.base_url.back() == '/') cfg.base_url.pop_back();
  for (auto& kv : taxonomies) {
    TaxonomyConfig t = kv.second;
    if (t.key.empty()) t.key = t.name;
    if (t.path.empty()) t.path = t.name;
    if (t.title.empty()) {
      t.title = t.name;
      t.title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t.title[0])));
    }
    cfg.taxonomies.push_back(t);
  }

  auto resolve = [&](const std::string& path_str) {
    std::filesystem::path path(path_str);
    if (path.is_relative()) path = cfg.content_dir / path;
    path = path.lexically_normal();
    if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
    return path;
  };

  cfg.dist_dir = resolve(dist);
  cfg.templates_dir = resolve(templates);
  cfg.skeleton_path = resolve(skeleton);
  if (!manifest.empty()) cfg.manifest_path = resolve(manifest);
  return cfg;
}
