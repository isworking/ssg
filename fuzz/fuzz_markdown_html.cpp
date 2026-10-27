// markdown_to_html and markdown_headings, with the default options and in strict CommonMark mode.
#include "markdown.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view md(reinterpret_cast<const char*>(data), size);
  try {
    markdown_to_html(md);
    markdown_headings(md);
    MarkdownOptions strict;
    strict.scripts = true;
    strict.extensions = false;
    strict.heading_ids = false;
    strict.autolinks = false;
    markdown_to_html(md, strict);
  } catch (const std::runtime_error&) {
  }
  return 0;
}
