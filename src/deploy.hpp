#pragma once

#include "config.hpp"

#include <string>
#include <vector>

// Deploy adapters: files generated for a hosting target. They only describe what a build
// published; nothing here changes the site itself.

// "cloudflare", "vercel" or "nginx".
bool known_deploy_target(const std::string& name);

struct DeployFile {
  std::string path;   // dist-relative when in_dist, else relative to the content dir
  bool in_dist;       // regenerated every build (dist is emptied) vs a project file
  std::string body;
};

// What the adapters read from a finished build.
struct DeployInput {
  std::string version;
  std::vector<std::string> immutable;  // fingerprinted file names, relative to dist
};

// The files for cfg.deploy_target; empty when there is none.
std::vector<DeployFile> deploy_files(const SiteConfig& cfg, const DeployInput& in);

// Names of project-root files the target generates, so the build does not publish them.
std::vector<std::string> deploy_project_files(const std::string& target);
