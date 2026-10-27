// The raw-HTML sanitizer: html = true, scripts = false, as in the default site.toml.
#include "markdown.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view md(reinterpret_cast<const char*>(data), size);
  MarkdownOptions opt;
  opt.raw_html = true;
  opt.scripts = false;
  try {
    markdown_to_html(md, opt);
  } catch (const std::runtime_error&) {
  }
  return 0;
}
