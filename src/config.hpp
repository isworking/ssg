#pragma once

#include "markdown.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct SiteConfig {
  std::filesystem::path content_dir;
  std::filesystem::path dist_dir;
  std::filesystem::path templates_dir;
  std::filesystem::path skeleton_path;

  // gitignore-style patterns from `ignore = [...]` in site.toml (on top of .assetignore files).
  std::vector<std::string> ignore;
  // Empty dist before building, so deleted pages don't linger.
  bool clean = true;
  // Build subdirectories that are git checkouts of their own (contain .git). Off by default, so
  // e.g. CI checking the builder out into ./builder doesn't publish it.
  bool nested_repos = false;
  // Build pages marked `draft: "true"` (set from the --drafts flag).
  bool drafts = false;
  // e.g. "https://isworking.dev". When set, sitemap.xml is written.
  std::string base_url;

  // From the [markdown] table.
  MarkdownOptions markdown;
};

SiteConfig load_site_config(const std::filesystem::path& content_dir);
