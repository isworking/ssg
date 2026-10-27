// Checks on what load_site_config() stores that the sites tests can't see from outside, such as
// the [plugin.<name>] tables kept for plugins. usage: config_test SCRATCH-DIR
#include "config.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (ok) return;
  std::cerr << "FAIL: " << what << "\n";
  ++failures;
}

SiteConfig load(const std::filesystem::path& dir, const std::string& extra) {
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "site.toml")
      << "dist = \"dist\"\ntemplates = \"templates\"\nskeleton = \"skeleton.html\"\n" << extra;
  return load_site_config(dir);
}

void plugin_tables(const std::filesystem::path& dir) {
  const SiteConfig cfg = load(dir, R"([plugin.search]
index.fields = ["title", "body"]
limit = 10
[plugin.search.ui]
theme = 'dark'
[plugin.empty]
[plugin."two words".x]
)");
  check(cfg.plugins.size() == 3, "three plugins");

  auto it = cfg.plugins.find("search");
  check(it != cfg.plugins.end(), "[plugin.search] is stored");
  if (it != cfg.plugins.end()) {
    const std::vector<TomlEntry>& es = it->second;
    check(es.size() == 3, "search has 3 entries");
    if (es.size() == 3) {
      // Paths are relative to [plugin.search], in file order.
      check(es[0].path == std::vector<std::string>{"index", "fields"}, "index.fields path");
      check(es[0].value.kind == TomlValue::Array && es[0].value.array.size() == 2 &&
                es[0].value.array[1].str == "body",
            "index.fields value");
      check(es[0].line == 5, "index.fields line");
      check(es[1].path == std::vector<std::string>{"limit"} && es[1].value.integer == 10,
            "limit");
      check(es[2].path == std::vector<std::string>{"ui", "theme"} && es[2].value.str == "dark",
            "ui.theme from [plugin.search.ui]");
    }
  }

  it = cfg.plugins.find("empty");
  check(it != cfg.plugins.end() && it->second.empty(), "[plugin.empty] is stored, empty");
  // A plugin named only by a sub-table header still gets its (empty) entry.
  it = cfg.plugins.find("two words");
  check(it != cfg.plugins.end() && it->second.empty(), "[plugin.\"two words\".x] is stored");
}

void no_plugins(const std::filesystem::path& dir) {
  check(load(dir, "[markdown]\nhtml = false\n").plugins.empty(), "no [plugin.*], no plugins");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: config_test SCRATCH-DIR\n";
    return 2;
  }
  const std::filesystem::path scratch = argv[1];
  std::filesystem::remove_all(scratch);
  try {
    plugin_tables(scratch / "plugins");
    no_plugins(scratch / "none");
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    ++failures;
  }
  std::filesystem::remove_all(scratch);
  if (failures == 0) std::cout << "config_test: ok\n";
  return failures == 0 ? 0 : 1;
}
