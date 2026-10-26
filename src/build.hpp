#pragma once

#include "config.hpp"

struct BuildOptions {
  // After building, warn about href="..." values that point at nothing in dist.
  bool check_links = false;
  // No summary line or notes (warnings still print).
  bool quiet = false;
};

struct BuildStats {
  size_t pages = 0;
  size_t files = 0;
  size_t warnings = 0;
};

BuildStats build_site(const SiteConfig& cfg, const BuildOptions& opt = {});
