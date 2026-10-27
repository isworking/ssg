#include "build.hpp"
#include "completions.hpp"
#include "config.hpp"
#include "deploy.hpp"
#include "markdown.hpp"
#include "plugin.hpp"
#include "scaffold.hpp"
#include "serve.hpp"
#include "util.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Both come from CMakeLists.txt.
#ifndef SSG_VERSION
#define SSG_VERSION "unknown"
#endif
#ifndef SSG_COMMIT
#define SSG_COMMIT "unknown"
#endif

// Exit codes.
constexpr int kOk = 0, kError = 1, kUsage = 2, kWarnings = 3;

const char* kUsageText =
    "usage: ssg [build] [--drafts] [--check-links] [options] [<content-dir>]\n"
    "       ssg init|serve|watch|check|list|clean|doctor [options] [<dir>]\n"
    "       ssg new <path>\n"
    "       ssg plugins list | plugins meta <name>\n"
    "       ssg render [--strict] [--text] [--no-html] [--scripts|--no-scripts]\n"
    "                  [--no-extensions] <file|->\n"
    "       ssg completions bash|zsh|fish\n"
    "       ssg --help | --version\n";

const char* kHelp =
    "Commands (<dir> defaults to the current directory; it must contain site.toml):\n"
    "\n"
    "  build [dir]        build the site into its dist dir (the default: `ssg <dir>` works)\n"
    "  init [dir]         scaffold site.toml, a skeleton, templates/ and an index page\n"
    "  new <path>         create a page with title and date frontmatter\n"
    "  serve [dir]        build, serve dist on 127.0.0.1 and reload open pages on change\n"
    "  watch [dir]        rebuild whenever something changes\n"
    "  check [dir]        broken links, orphan pages, images without alt text, unknown\n"
    "                     template keys; builds into a temp dir, so dist is untouched\n"
    "  list [dir]         list pages and their URLs\n"
    "  clean [dir]        empty dist, but only if ssg made it\n"
    "  doctor [dir]       check the config, dist, skeleton and plugins\n"
    "  plugins list       show the [plugin.<name>] tables; plugins meta <name> shows one\n"
    "  render <file|->    markdown on stdout, no frontmatter or templates\n"
    "  completions <sh>   print a completion script for bash, zsh or fish\n"
    "\n"
    "Options (after the command):\n"
    "\n"
    "  --config <file>    read this file instead of <dir>/site.toml\n"
    "  --output <dir>     build into <dir> instead of the configured dist\n"
    "  --base-url <url>   override base_url\n"
    "  --drafts           also build pages with draft: \"true\" (build, serve, watch, check, list)\n"
    "  --check-links      warn about local href/src targets missing from dist (build)\n"
    "  --incremental      keep dist and rewrite only what changed (build; serve and watch always do)\n"
    "  --target <name>    also write the deploy files for cloudflare, vercel or nginx (build)\n"
    "  --force            let --target replace adapter files that exist and differ (build)\n"
    "  --port <n>         serve on this port (default 8000; 0 picks a free one)\n"
    "  --open             open the served site in a browser\n"
    "  --verbose          one line per page and file\n"
    "  --quiet, -q        no summary line\n"
    "  --color <when>     auto (default), always or never; NO_COLOR is respected\n"
    "  --json             machine-readable result on stdout; messages stay on stderr\n"
    "  --fail-on-warning  exit 3 if there were warnings\n"
    "  --no-plugins       don't run plugins\n"
    "\n"
    "Exit status: 0 ok, 1 error, 2 usage error, 3 warnings with --fail-on-warning.\n"
    "\n"
    "render: markdown on stdout, no frontmatter or templates.\n"
    "  --strict         CommonMark only: raw HTML and scripts pass through, no extensions,\n"
    "                   heading ids, or bare autolinks\n"
    "  --text           plain text instead of HTML\n"
    "  --no-html        escape raw HTML\n"
    "  --scripts        let <script>, on* attributes and javascript: URLs through\n"
    "  --no-scripts     strip them (the default)\n"
    "  --no-extensions  strict CommonMark syntax\n";

int usage_error(const std::string& msg) {
  if (!msg.empty()) std::cerr << "ssg: " << msg << "\n";
  std::cerr << kUsageText;
  return kUsage;
}

struct Opts {
  std::string config, output, base_url, target, color = "auto";
  bool verbose = false, quiet = false, json = false, fail_on_warning = false, no_plugins = false;
  bool drafts = false, check_links = false, open = false, force = false,
       incremental = false;
  int port = 8000;
  std::vector<std::string> pos;
};

// What parse_opts decided: 0 to go on, -1 if --help was printed, else an exit code.
struct Parsed {
  int status = 0;
};

// Parses argv[start..]. `local` names the flags (without --) this command accepts on top of the
// global ones.
Parsed parse_opts(int argc, char** argv, int start, const std::set<std::string>& local, Opts& o) {
  for (int i = start; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::cout << kUsageText << "\n" << kHelp;
      return {-1};
    }
    if (a == "-q") a = "--quiet";
    if (a.size() < 2 || a[0] != '-') {
      o.pos.push_back(a);
      continue;
    }
    std::string value;
    bool has_value = false;
    if (const size_t eq = a.find('='); a.compare(0, 2, "--") == 0 && eq != std::string::npos) {
      value = a.substr(eq + 1);
      a.resize(eq);
      has_value = true;
    }
    static const std::set<std::string> globals = {"config",  "output",  "base-url",
                                                  "verbose", "quiet",   "color",
                                                  "json",    "fail-on-warning", "no-plugins"};
    if (a.compare(0, 2, "--") != 0) return {usage_error("unknown option " + a)};
    const std::string name = a.substr(2);
    if (!globals.count(name) && !local.count(name)) {
      return {usage_error("unknown option " + a)};
    }
    auto need_value = [&](std::string& dst) {
      if (!has_value) {
        if (i + 1 >= argc) return false;
        value = argv[++i];
      }
      dst = value;
      return true;
    };
    std::string port;
    if (name == "config" || name == "output" || name == "base-url" || name == "color" ||
        name == "port" || name == "target") {
      std::string& dst = name == "config"     ? o.config
                         : name == "output"   ? o.output
                         : name == "base-url" ? o.base_url
                         : name == "color"    ? o.color
                         : name == "target"   ? o.target
                                              : port;
      if (!need_value(dst)) return {usage_error("--" + name + " needs a value")};
      if (name == "color" && o.color != "auto" && o.color != "always" && o.color != "never") {
        return {usage_error("--color wants auto, always or never")};
      }
      if (name == "target" && !known_deploy_target(o.target)) {
        return {usage_error("--target wants cloudflare, vercel or nginx")};
      }
      if (name == "port") {
        char* end = nullptr;
        const long v = std::strtol(port.c_str(), &end, 10);
        if (port.empty() || *end != '\0' || v < 0 || v > 65535) {
          return {usage_error("--port wants a number from 0 to 65535")};
        }
        o.port = static_cast<int>(v);
      }
      continue;
    }
    if (has_value) return {usage_error("--" + name + " takes no value")};
    if (name == "verbose") o.verbose = true;
    else if (name == "quiet") o.quiet = true;
    else if (name == "json") o.json = true;
    else if (name == "fail-on-warning") o.fail_on_warning = true;
    else if (name == "no-plugins") o.no_plugins = true;
    else if (name == "drafts") o.drafts = true;
    else if (name == "check-links") o.check_links = true;
    else if (name == "incremental") o.incremental = true;
    else if (name == "open") o.open = true;
    else if (name == "force") o.force = true;
  }
  if (o.color == "always") use_color() = true;
  else if (o.color == "never") use_color() = false;
  else {
    const char* no = std::getenv("NO_COLOR");
    const char* term = std::getenv("TERM");
    use_color() = isatty(2) && !(no && *no) && !(term && std::string_view(term) == "dumb");
  }
  return {0};
}

// --- loading

SiteConfig load(const Opts& o, const std::string& dir) {
  SiteConfig cfg = load_site_config(dir, o.config);
  cfg.drafts = o.drafts;
  if (!o.target.empty()) cfg.deploy_target = o.target;
  cfg.deploy_force = o.force;
  if (!o.output.empty()) {
    // The configured dist may hold an earlier build; it is never content.
    cfg.skip_dirs.push_back(cfg.dist_dir);
    cfg.dist_dir = fs::absolute(o.output).lexically_normal();
    cfg.manifest_path.clear();  // the configured one belongs to the configured dist
  }
  if (!o.base_url.empty()) {
    cfg.base_url = o.base_url;
    while (!cfg.base_url.empty() && cfg.base_url.back() == '/') cfg.base_url.pop_back();
  }
  return cfg;
}

std::string dir_arg(const Opts& o, std::string& err) {
  if (o.pos.size() > 1) err = "one content dir only";
  return o.pos.empty() ? "." : o.pos[0];
}

// --- JSON helpers (schema 1)

std::string diags_json(const std::vector<Diag>& a, const std::vector<Diag>& b) {
  std::string out = "[";
  bool first = true;
  for (const auto* v : {&a, &b}) {
    for (const Diag& d : *v) {
      out += (first ? "" : ",") + std::string("{\"file\":") + json_str(d.file) +
             ",\"message\":" + json_str(d.message) + "}";
      first = false;
    }
  }
  return out + "]";
}

std::string pages_json(const std::vector<PageInfo>& pages) {
  std::string out = "[";
  for (size_t i = 0; i < pages.size(); ++i) {
    out += (i ? "," : "") + std::string("{\"url\":") + json_str(pages[i].url) +
           ",\"source\":" + json_str(pages[i].source) + ",\"output\":" + json_str(pages[i].output) +
           "}";
  }
  return out + "]";
}

int finish(const Opts& o, size_t warnings) {
  return o.fail_on_warning && warnings ? kWarnings : kOk;
}

BuildOptions build_options(const Opts& o) {
  BuildOptions b;
  b.check_links = o.check_links;
  b.incremental = o.incremental;
  b.quiet = o.quiet || o.json;
  b.verbose = o.verbose;
  b.version = SSG_VERSION;
  b.no_plugins = o.no_plugins;
  return b;
}

// A scratch dist for commands that build only to look: removed when it goes out of scope.
struct TempDist {
  fs::path path;
  TempDist() {
    static int n = 0;
    path = fs::temp_directory_path() /
           ("ssg-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
  }
  ~TempDist() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

// --- commands

int render_cmd(int argc, char** argv) {
  MarkdownOptions opt;
  bool text = false;
  std::string input;
  bool have_input = false;
  for (int i = 2; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--strict") {
      opt.raw_html = true;
      opt.scripts = true;
      opt.extensions = false;
      opt.heading_ids = false;
      opt.autolinks = false;
    } else if (a == "--text") {
      text = true;
    } else if (a == "--no-html") {
      opt.raw_html = false;
    } else if (a == "--scripts") {
      opt.scripts = true;
    } else if (a == "--no-scripts") {
      opt.scripts = false;
    } else if (a == "--no-extensions") {
      opt.extensions = false;
    } else if (a == "-h" || a == "--help") {
      std::cout << kUsageText << "\n" << kHelp;
      return kOk;
    } else if (a.size() > 1 && a[0] == '-') {
      return usage_error("unknown option " + std::string(a));
    } else if (have_input) {
      return usage_error("render takes one input");
    } else {
      input = std::string(a);
      have_input = true;
    }
  }
  if (!have_input) return usage_error("render needs a file or -");

  std::string md;
  if (input == "-") {
    md.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
  } else {
    md = read_file(input);
  }
  std::cout << (text ? markdown_to_text(md, opt) : markdown_to_html(md, opt));
  std::cout.flush();
  if (!std::cout) {
    std::cerr << "ssg: error writing output\n";
    return kError;
  }
  return kOk;
}

int build_cmd(const Opts& o) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  SiteConfig cfg = load(o, dir);
  BuildOptions b = build_options(o);
  b.deploy = true;
  const BuildStats st = build_site(cfg, b);
  const size_t warnings = cfg.warnings.size() + st.warnings;
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"build\",\"dist\":"
              << json_str(cfg.dist_dir.string()) << ",\"manifest\":"
              << (st.manifest.empty() ? "null" : json_str(st.manifest)) << ",\"pages\":"
              << pages_json(st.page_list) << ",\"files\":[";
    for (size_t i = 0; i < st.file_list.size(); ++i) {
      std::cout << (i ? "," : "") << json_str(st.file_list[i]);
    }
    std::cout << "],\"warnings\":" << diags_json(cfg.warnings, st.diags)
              << ",\"timings_ms\":{\"total\":" << st.ms << "}}\n";
  }
  return finish(o, warnings);
}

int check_cmd(const Opts& o) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  SiteConfig cfg = load(o, dir);
  TempDist tmp;
  cfg.skip_dirs.push_back(cfg.dist_dir);
  cfg.dist_dir = tmp.path;
  cfg.write_manifest = false;
  BuildOptions b = build_options(o);
  b.quiet = true;
  b.check_links = true;
  b.extra_checks = true;
  const BuildStats st = build_site(cfg, b);
  const size_t warnings = cfg.warnings.size() + st.warnings;
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"check\",\"pages\":" << st.pages
              << ",\"warnings\":" << diags_json(cfg.warnings, st.diags) << "}\n";
  }
  if (!o.quiet) {
    std::cerr << "ssg: checked " << st.pages << " page" << (st.pages == 1 ? "" : "s") << ", "
              << warnings << " warning" << (warnings == 1 ? "" : "s") << "\n";
  }
  return finish(o, warnings);
}

int list_cmd(const Opts& o) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  SiteConfig cfg = load(o, dir);
  TempDist tmp;
  cfg.skip_dirs.push_back(cfg.dist_dir);
  cfg.dist_dir = tmp.path;
  cfg.write_manifest = false;
  BuildOptions b = build_options(o);
  b.quiet = true;
  const BuildStats st = build_site(cfg, b);
  std::vector<PageInfo> pages = st.page_list;
  std::sort(pages.begin(), pages.end(),
            [](const PageInfo& a, const PageInfo& c) { return a.url < c.url; });
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"list\",\"pages\":" << pages_json(pages)
              << ",\"warnings\":" << diags_json(cfg.warnings, st.diags) << "}\n";
  } else {
    size_t w = 0;
    for (const auto& p : pages) w = std::max(w, p.url.size());
    for (const auto& p : pages) {
      std::cout << p.url << std::string(w - p.url.size() + 2, ' ') << p.source << "\n";
    }
  }
  return finish(o, cfg.warnings.size() + st.warnings);
}

int clean_cmd(const Opts& o) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  const SiteConfig cfg = load(o, dir);
  std::string why;
  if (!clean_dist(cfg, why)) {
    throw std::runtime_error("not cleaning " + cfg.dist_dir.string() + ": " + why);
  }
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"clean\",\"dist\":"
              << json_str(cfg.dist_dir.string()) << "}\n";
  }
  if (!o.quiet && !o.json) std::cerr << "ssg: cleaned " << cfg.dist_dir.string() << "\n";
  return finish(o, cfg.warnings.size());
}

int doctor_cmd(const Opts& o) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  std::vector<Check> checks;
  try {
    const SiteConfig cfg = load(o, dir);
    checks = doctor_site(cfg);
  } catch (const std::exception& e) {
    checks.push_back({"error", "config", e.what()});
  }
  size_t errors = 0, warns = 0;
  for (const Check& c : checks) {
    errors += c.status == "error";
    warns += c.status == "warn";
  }
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":" << (errors ? "false" : "true")
              << ",\"command\":\"doctor\",\"checks\":[";
    for (size_t i = 0; i < checks.size(); ++i) {
      std::cout << (i ? "," : "") << "{\"status\":" << json_str(checks[i].status)
                << ",\"name\":" << json_str(checks[i].name)
                << ",\"message\":" << json_str(checks[i].message) << "}";
    }
    std::cout << "]}\n";
  } else {
    for (const Check& c : checks) {
      std::cout << (c.status == "ok" ? "ok    " : c.status == "warn" ? "warn  " : "error ")
                << c.name << ": " << c.message << "\n";
    }
  }
  if (errors) return kError;
  return finish(o, warns);
}

int plugins_cmd(const Opts& o) {
  if (o.pos.empty() || (o.pos[0] != "list" && o.pos[0] != "meta")) {
    return usage_error("plugins needs list or meta <name>");
  }
  const bool meta = o.pos[0] == "meta";
  if (meta ? o.pos.size() != 2 : o.pos.size() != 1) {
    return usage_error(meta ? "plugins meta needs one plugin name" : "plugins list takes no arguments");
  }
  const SiteConfig cfg = load(o, ".");
  const std::vector<PluginSpec> specs = plugin_specs(cfg);
  if (meta) {
    for (const PluginSpec& spec : specs) {
      if (spec.name != o.pos[1]) continue;
      std::string why, raw;
      const fs::path exe = find_plugin(cfg, spec, why);
      if (exe.empty()) throw std::runtime_error("plugin '" + spec.name + "': " + why);
      read_plugin_meta(exe, spec.timeout, &raw);
      std::cout << raw << "\n";
      return finish(o, cfg.warnings.size());
    }
    throw std::runtime_error("no [plugin." + o.pos[1] + "] table in " + cfg.config_path.string());
  }
  if (o.json) std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"plugins\",\"plugins\":[";
  bool first = true;
  for (const PluginSpec& spec : specs) {
    std::string why, version, error;
    int protocol = 0;
    std::vector<std::string> hooks;
    fs::path exe;
    if (spec.enabled) {
      exe = find_plugin(cfg, spec, why);
      if (exe.empty()) {
        error = why;
      } else {
        try {
          const PluginMeta m = read_plugin_meta(exe, spec.timeout);
          version = m.version;
          protocol = m.protocol;
          hooks = m.hooks;
          if (m.protocol > kPluginProtocol) error = "needs a newer plugin protocol than ssg speaks";
        } catch (const std::exception& e) {
          error = e.what();
        }
      }
    }
    if (o.json) {
      std::cout << (first ? "" : ",") << "{\"name\":" << json_str(spec.name)
                << ",\"enabled\":" << (spec.enabled ? "true" : "false")
                << ",\"required\":" << (spec.required ? "true" : "false")
                << ",\"resolved\":" << (error.empty() && spec.enabled ? "true" : "false");
      if (!exe.empty()) std::cout << ",\"path\":" << json_str(exe.string());
      if (!version.empty()) std::cout << ",\"version\":" << json_str(version) << ",\"protocol\":" << protocol;
      std::cout << ",\"hooks\":[";
      for (size_t i = 0; i < hooks.size(); ++i) std::cout << (i ? "," : "") << json_str(hooks[i]);
      std::cout << "]";
      if (!error.empty()) std::cout << ",\"error\":" << json_str(error);
      std::cout << "}";
    } else if (!spec.enabled) {
      std::cout << spec.name << "  disabled\n";
    } else if (!error.empty()) {
      std::cout << spec.name << "  " << (spec.required ? "error" : "warning") << ": " << error << "\n";
    } else {
      std::cout << spec.name << "  " << version << "  " << exe.string() << "\n";
    }
    first = false;
  }
  if (o.json) std::cout << "]}\n";
  return finish(o, cfg.warnings.size());
}

int init_cmd(const Opts& o) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  const auto created = scaffold_site(dir);
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"init\",\"created\":[";
    for (size_t i = 0; i < created.size(); ++i) std::cout << (i ? "," : "") << json_str(created[i]);
    std::cout << "]}\n";
  }
  if (!o.quiet) {
    for (const auto& f : created) std::cerr << "ssg: created " << f << "\n";
    std::cerr << "ssg: next: ssg serve " << dir << "\n";
  }
  return kOk;
}

int new_cmd(const Opts& o) {
  if (o.pos.size() != 1) return usage_error("new takes one path");
  const std::string file = scaffold_page(o.pos[0]);
  if (o.json) {
    std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"new\",\"created\":" << json_str(file)
              << "}\n";
  }
  if (!o.quiet) std::cerr << "ssg: created " << file << "\n";
  return kOk;
}

int completions_cmd(const Opts& o) {
  if (o.pos.size() != 1) return usage_error("completions needs bash, zsh or fish");
  const char* script = completion_script(o.pos[0].c_str());
  if (!script) return usage_error("unknown shell " + o.pos[0] + " (want bash, zsh or fish)");
  std::cout << script;
  return kOk;
}

int watch_or_serve(const Opts& o, bool serve) {
  std::string err;
  const std::string dir = dir_arg(o, err);
  if (!err.empty()) return usage_error(err);
  const SiteConfig first = load(o, dir);
  BuildOptions b = build_options(o);
  b.quiet = o.quiet;
  b.incremental = true;  // unchanged files keep their mtime and dist is never empty mid-build
  // The config is read again each time, so edits to site.toml take effect (dist excepted).
  auto rebuild = [&]() {
    SiteConfig cfg = load(o, dir);
    cfg.dist_dir = first.dist_dir;
    cfg.write_manifest = false;  // it may sit in the watched tree and retrigger the build
    build_site(cfg, b);
    b.notes = false;  // the next rebuilds would only repeat them
  };
  if (!serve) return watch_site(first.content_dir, first.dist_dir, rebuild);
  ServeOptions so;
  so.port = o.port;
  so.open = o.open;
  return serve_site(first.content_dir, first.dist_dir, so, rebuild);
}

struct Command {
  const char* name;
  std::set<std::string> local;  // flags beyond the global ones
};

const std::vector<Command> kCommands = {
    {"build", {"drafts", "check-links", "target", "force", "incremental"}},
    {"init", {}},
    {"new", {}},
    {"serve", {"drafts", "port", "open"}},
    {"watch", {"drafts"}},
    {"check", {"drafts"}},
    {"list", {"drafts"}},
    {"clean", {}},
    {"plugins", {}},
    {"doctor", {}},
    {"completions", {}},
};

int dispatch(const std::string& name, const Opts& o) {
  if (name == "build") return build_cmd(o);
  if (name == "init") return init_cmd(o);
  if (name == "new") return new_cmd(o);
  if (name == "serve") return watch_or_serve(o, true);
  if (name == "watch") return watch_or_serve(o, false);
  if (name == "check") return check_cmd(o);
  if (name == "list") return list_cmd(o);
  if (name == "clean") return clean_cmd(o);
  if (name == "plugins") return plugins_cmd(o);
  if (name == "doctor") return doctor_cmd(o);
  return completions_cmd(o);
}

}  // namespace

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  if (argc >= 2) {
    const std::string_view a = argv[1];
    if (a == "--help" || a == "-h") {
      std::cout << kUsageText << "\n" << kHelp;
      return kOk;
    }
    if (a == "--version") {
      const std::string_view commit = SSG_COMMIT;
      std::cout << "ssg " << SSG_VERSION;
      if (commit != "unknown") std::cout << " (" << commit << ")";
      std::cout << "\n";
      return kOk;
    }
  }
  if (argc < 2) return usage_error("");

  Opts o;
  try {
    const std::string first = argv[1];
    if (first == "render") return render_cmd(argc, argv);
    const Command* cmd = nullptr;
    for (const Command& c : kCommands) {
      if (first == c.name) cmd = &c;
    }
    // Not a command name: `ssg [flags] <dir>` is `ssg build [flags] <dir>`. Flags may also come
    // before the command (`ssg --json build .`): the first word then names the command.
    Parsed p;
    if (cmd) {
      p = parse_opts(argc, argv, 2, cmd->local, o);
    } else {
      std::set<std::string> all;
      for (const Command& c : kCommands) all.insert(c.local.begin(), c.local.end());
      p = parse_opts(argc, argv, 1, all, o);
      if (p.status == 0 && !o.pos.empty()) {
        for (const Command& c : kCommands) {
          if (o.pos[0] == c.name) cmd = &c;
        }
        if (cmd) {
          o.pos.erase(o.pos.begin());
          const std::vector<std::string> rest = o.pos;
          o = Opts();
          p = parse_opts(argc, argv, 1, cmd->local, o);
          if (p.status == 0) o.pos = rest;
        }
      }
    }
    if (p.status == -1) return kOk;
    if (p.status != 0) return p.status;
    return dispatch(cmd ? cmd->name : "build", o);
  } catch (const std::exception& e) {
    std::cerr << "ssg: " << e.what() << "\n";
    if (o.json) {
      std::cout << "{\"schema\":1,\"ok\":false,\"error\":" << json_str(e.what()) << "}\n";
    }
    return kError;
  }
}
