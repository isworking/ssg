#pragma once

#include "markdown.hpp"
#include "toml.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// A warning: the file (or "") it is about, and the message.
struct Diag {
  std::string file;
  std::string message;
};

// The [robots] table.
struct RobotsConfig {
  bool enabled = true;   // write robots.txt unless the content dir has one
  bool table = false;    // the table was written, so a hand-written robots.txt is worth a warning
  bool sitemap = true;   // add a Sitemap: line when sitemap.xml is written
  std::vector<std::string> disallow;
};

// A [taxonomy.<name>] table.
struct TaxonomyConfig {
  std::string name;
  std::string key;    // the frontmatter key holding the comma-separated terms; default name
  std::string path;   // where its pages go: /<path>/ and /<path>/<term>/; default name
  std::string title;  // heading of the index page; default name, capitalised
};

// The [feed] table: its presence turns feed.xml on.
struct FeedConfig {
  bool enabled = false;
  std::string section;  // pages under this directory ("" for every dated page)
  std::string title;    // "" means the host name of base_url
  std::string author;
  int limit = 20;
};

// The [css] table.
struct CssConfig {
  bool minify = false;          // minify every .css file, published or inlined
  size_t inline_limit = 16384;  // warn when an {{inline}}d stylesheet is larger (bytes; 0: never)
};

// The [headers] table: what the generated adapters send. Vary: Accept and Content-Type are always
// sent, so they have no switch.
struct HeadersConfig {
  bool link = true;      // Link: <.../index.md>; rel="alternate" on HTML pages
  bool format = true;    // X-SSG-Format: html, md or txt
  bool version = false;  // X-SSG-Version: the generator version (opt-in)
  std::string page_cache = "public, max-age=300";  // Cache-Control for pages; "" for none
};

struct SiteConfig {
  std::filesystem::path content_dir;
  std::filesystem::path dist_dir;
  std::filesystem::path templates_dir;
  std::filesystem::path skeleton_path;
  // The config file that was read (content_dir/site.toml unless --config said otherwise).
  std::filesystem::path config_path;

  // gitignore-style patterns from `ignore = [...]` in site.toml (on top of .assetignore files).
  std::vector<std::string> ignore;
  // Empty dist before building, so deleted pages don't linger.
  bool clean = true;
  // Build subdirectories that are git checkouts of their own (contain .git). Off by default, so
  // e.g. CI checking the builder out into ./builder doesn't publish it.
  bool nested_repos = false;
  // Copy files that are symlinks to a file. Off by default: a link in untrusted content could
  // otherwise publish any file the build can read.
  bool follow_symlinks = false;
  // Directories to leave out of the walk on top of dist, e.g. the real dist while a command
  // builds into a scratch one.
  std::vector<std::filesystem::path> skip_dirs;
  // Build pages marked `draft: "true"` (set from the --drafts flag).
  bool drafts = false;
  // Where manifest.json goes; empty means beside dist. `manifest = false` in site.toml turns the
  // file off (and so does the check and list commands, which build into a scratch dir).
  bool write_manifest = true;
  std::filesystem::path manifest_path;
  // Build pages whose `date` is after the build date (otherwise they count as scheduled drafts).
  bool future = false;
  // Also write an HTML page with a meta refresh at each redirect's old URL, for hosts that do
  // not read _redirects.
  bool redirect_pages = false;
  // e.g. "https://isworking.dev". When set, sitemap.xml is written.
  std::string base_url;

  std::vector<TaxonomyConfig> taxonomies;  // sorted by name
  FeedConfig feed;
  RobotsConfig robots;
  CssConfig css;

  // [deploy] target (or --target): "cloudflare", "vercel", "nginx" or "" for none.
  std::string deploy_target;
  // Replace adapter files that exist already (set from --force).
  bool deploy_force = false;
  HeadersConfig headers;

  // From the [markdown] table.
  MarkdownOptions markdown;

  // [plugin.<name>] tables, keyed by name; entry paths are relative to the plugin's table.
  // A plugin with an empty table still gets an (empty) entry.
  std::map<std::string, std::vector<TomlEntry>> plugins;
  // The same names in the order site.toml first mentions them, which is the order they run in.
  std::vector<std::string> plugin_order;

  // Warnings raised while reading the config (also printed as they happen).
  std::vector<Diag> warnings;
};

// `config_file` empty means content_dir/site.toml.
SiteConfig load_site_config(const std::filesystem::path& content_dir,
                            const std::filesystem::path& config_file = {});
