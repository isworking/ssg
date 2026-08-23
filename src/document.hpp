#pragma once

#include "config.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

struct Segment {
  bool is_key = false;
  std::string text;
};

struct ParsedDoc {
  std::map<std::string, std::string> vars;
  std::string extend_file;
  std::string body;
};

struct RenderedDoc {
  std::string markdown;
  std::map<std::string, std::string> vars;
};

std::vector<Segment> parse_segments(std::string_view s);
ParsedDoc parse_markdown_source(std::string_view source);
std::string interpolate(std::string_view s, const std::map<std::string, std::string>& vars);
RenderedDoc render_extended(const SiteConfig& cfg, const std::filesystem::path& source_path,
                            const ParsedDoc& doc);
