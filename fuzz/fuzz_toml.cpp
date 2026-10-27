// The site.toml parser.
#include "toml.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  try {
    const TomlDoc doc = parse_toml(text, "site.toml");
    for (const auto& e : doc.entries) {
      toml_path_string(e.path);
      toml_kind_name(e.value.kind);
    }
  } catch (const std::runtime_error&) {
  }
  return 0;
}
