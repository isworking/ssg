// The JSON reader behind the plugin protocol.
#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  try {
    // What ssg prints must read back as the same document.
    const Json a = parse_json(text);
    const std::string once = a.dump();
    if (parse_json(once).dump() != once) __builtin_trap();
  } catch (const std::runtime_error&) {
  }
  return 0;
}
