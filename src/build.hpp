#pragma once

#include "config.hpp"

#include <string>
#include <vector>

struct BuildOptions {
  // After building, warn about href="..." values that point at nothing in dist.
  bool check_links = false;
  // No summary line or notes (warnings still print).
  bool quiet = false;
  // Repeatable notes (skipped symlinks); serve and watch show them on the first build only.
  bool notes = true;
  // One line per page and per copied file on stderr.
  bool verbose = false;
  // What `ssg check` adds: pages nothing links to, images without alt text, and {{keys}} that
  // nothing defines.
  bool extra_checks = false;
  // Write the deploy target's adapter files (only `ssg build` does; check and serve build to
  // look, not to deploy).
  bool deploy = false;
  // Don't start the plugins configured in site.toml (--no-plugins).
  bool no_plugins = false;
  // Keep dist: rewrite only files whose bytes changed and remove the ones no longer produced.
  // The result is the same as a clean build.
  bool incremental = false;
  // The generator version recorded in manifest.json.
  std::string version = "unknown";
};

struct PageInfo {
  std::string url;     // "/a/b/"
  std::string source;  // relative to the content dir: "a/b.md"
  std::string output;  // published markdown, relative to dist: "a/b/index.md"
};

struct BuildStats {
  size_t pages = 0;
  size_t files = 0;
  size_t warnings = 0;
  long long ms = 0;
  size_t changed = 0, unchanged = 0;  // incremental: files rewritten vs left as they were
  std::string manifest;  // path of the manifest.json written, "" if none
  std::vector<PageInfo> page_list;
  std::vector<std::string> file_list;  // copied files, relative to dist
  std::vector<Diag> diags;             // the warnings, in order (not the config's)
};

BuildStats build_site(const SiteConfig& cfg, const BuildOptions& opt = {});

// Empties dist if ssg owns it. False, with `why` set, if it doesn't.
bool clean_dist(const SiteConfig& cfg, std::string& why);

struct Check {
  std::string status;  // "ok", "warn" or "error"
  std::string name;
  std::string message;
};
// Read-only checks of the config, dist, skeleton and templates; never throws.
std::vector<Check> doctor_site(const SiteConfig& cfg);
