// A page through frontmatter parsing and template evaluation (keys, {{if}}, escapes). The templates
// dir doesn't exist, so {{extend}} and {{include}} end in a "missing template" error.
#include "config.hpp"
#include "document.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view src(reinterpret_cast<const char*>(data), size);
  static const SiteConfig cfg = [] {
    SiteConfig c;
    c.content_dir = "/nonexistent-ssg-fuzz";
    c.templates_dir = "/nonexistent-ssg-fuzz/templates";
    return c;
  }();
  try {
    const ParsedDoc doc = parse_markdown_source(src);
    render_extended(cfg, "fuzz.md", doc, {{"url", "/fuzz"}, {"title", "Fuzz"}});
    interpolate(doc.body, doc.vars);
  } catch (const std::runtime_error&) {
  }
  return 0;
}
