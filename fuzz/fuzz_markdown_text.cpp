// markdown_to_text at the default width and at a narrow one, which wraps far more often.
#include "markdown.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view md(reinterpret_cast<const char*>(data), size);
  try {
    markdown_to_text(md);
    MarkdownOptions narrow;
    narrow.text_width = 12;
    markdown_to_text(md, narrow);
  } catch (const std::runtime_error&) {
  }
  return 0;
}
