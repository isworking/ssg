#pragma once

#include "config.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct Segment {
  bool is_key = false;
  std::string text;
};

// What {{asset "path"}} and {{inline "path"}} expand to; both throw std::runtime_error to fail
// the build. Without hooks the two tags stay as written.
struct AssetHooks {
  std::function<std::string(const std::string&)> asset;
  std::function<std::string(const std::string&)> inline_css;
};

struct ParsedDoc {
  std::map<std::string, std::string> vars;
  std::string extend_file;
  std::string body;
};

struct RenderedDoc {
  std::string markdown;
  std::map<std::string, std::string> vars;
  // Keys the final pass left as written because nothing defines them (sorted, no repeats).
  std::vector<std::string> unknown_keys;
};

std::vector<Segment> parse_segments(std::string_view s);
ParsedDoc parse_markdown_source(std::string_view source);
std::string interpolate(std::string_view s, const std::map<std::string, std::string>& vars);
// `defaults` are the lowest-priority vars (builtins like url); templates and the page override
// them. The result has template syntax fully resolved and \{{ escapes turned into literal {{.
RenderedDoc render_extended(const SiteConfig& cfg, const std::filesystem::path& source_path,
                            const ParsedDoc& doc,
                            const std::map<std::string, std::string>& defaults = {},
                            const AssetHooks* hooks = nullptr);
// Defined, non-empty, and not "false"/"0"/"no"/"off".
bool is_truthy(std::string_view v);
