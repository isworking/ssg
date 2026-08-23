#pragma once

#include <filesystem>
#include <string>

struct SiteConfig {
  std::filesystem::path content_dir;
  std::filesystem::path dist_dir;
  std::filesystem::path templates_dir;
  std::filesystem::path skeleton_path;
};

SiteConfig load_site_config(const std::filesystem::path& content_dir);
