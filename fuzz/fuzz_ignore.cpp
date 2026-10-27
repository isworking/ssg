// gitignore-style rules. Every line but the last is a pattern; the last line is the path to test,
// as a file and as a directory. The first pattern line is also matched alone with glob_match.
#include "ignore.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view in(reinterpret_cast<const char*>(data), size);
  const size_t nl = in.rfind('\n');
  const std::string_view patterns = nl == std::string_view::npos ? in : in.substr(0, nl);
  const std::string_view path = nl == std::string_view::npos ? in : in.substr(nl + 1);
  try {
    IgnoreRules rules;
    rules.add_file("", patterns);
    rules.add("sub/dir", patterns.substr(0, patterns.find('\n')));
    rules.ignored(path, false);
    rules.ignored(path, true);
    glob_match(patterns.substr(0, patterns.find('\n')), path);
  } catch (const std::runtime_error&) {
  }
  return 0;
}
