#pragma once

#include <string>
#include <string_view>
#include <vector>

struct MarkdownOptions {
  // Pass raw HTML (blocks and inline tags) through. When false, it is escaped and shown as text.
  bool raw_html = true;
  // Only matters when raw_html is on. When false, <script>/<iframe>/<object>/<embed>/<style>
  // and friends are escaped, on* event-handler attributes are dropped, and javascript:/vbscript:/
  // data: (non-image) URLs are neutralized, in raw HTML and in markdown links/images alike.
  bool scripts = false;
  // Give headings an id (GitHub-style slug, deduplicated) unless one is set with {#id}.
  bool heading_ids = true;
  // GFM bare autolinks: www.example.com, https://example.com, user@example.com.
  bool autolinks = true;
  // Everything beyond CommonMark: tables, strikethrough, task lists, footnotes, definition lists,
  // ::: containers, > [!NOTE] alerts, ==mark==, heading attributes. Off = strict CommonMark.
  bool extensions = true;
  // Column at which the plain-text renderer wraps paragraphs. 0 disables wrapping.
  int text_width = 80;
};

struct Heading {
  int level = 0;
  std::string text;  // plain text, no markup
  std::string id;    // empty when heading_ids is off and no {#id} was given
};

std::string markdown_to_html(std::string_view md, const MarkdownOptions& opt = {});
std::string markdown_to_text(std::string_view md, const MarkdownOptions& opt = {});
// Headings in document order, with the same ids markdown_to_html assigns.
std::vector<Heading> markdown_headings(std::string_view md, const MarkdownOptions& opt = {});
