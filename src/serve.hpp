#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

// A fingerprint of every non-hidden file under `content` (paths, sizes, mtimes), skipping `skip`
// (dist). It changes when a page, asset, template or the config is added, removed or edited.
uint64_t tree_signature(const std::filesystem::path& content, const std::filesystem::path& skip);

// Polls `content` and calls `rebuild` whenever it changes. Runs until killed. `rebuild` may
// throw; the error is printed and watching goes on.
int watch_site(const std::filesystem::path& content, const std::filesystem::path& dist,
               const std::function<void()>& rebuild);

struct ServeOptions {
  int port = 8000;  // 0 picks a free one
  bool open = false;
  bool quiet = false;
};

// Serves `dist` on 127.0.0.1 (static files, the site's 404.html for misses) and rebuilds on
// change; open pages reload themselves. Runs until killed.
int serve_site(const std::filesystem::path& content, const std::filesystem::path& dist,
               const ServeOptions& opt, const std::function<void()>& rebuild);
