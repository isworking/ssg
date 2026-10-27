#include <algorithm>
#include "scaffold.hpp"
#include "util.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

const char* kSiteToml =
    "dist = \"dist\"\n"
    "templates = \"templates\"\n"
    "skeleton = \"skeleton.html\"\n"
    "\n"
    "# Set this to write sitemap.xml.\n"
    "# base_url = \"https://example.com\"\n";

const char* kSkeleton =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\">\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "  <title></title>\n"
    "</head>\n"
    "<body>\n"
    "<main>\n"
    "</main>\n"
    "</body>\n"
    "</html>\n";

const char* kBaseTemplate =
    "---\n"
    "title: \"{{page_title}}\"\n"
    "---\n"
    "\n"
    "# {{page_title}}\n"
    "\n"
    "{{content}}\n";

const char* kIndex =
    "---\n"
    "page_title: \"Home\"\n"
    "---\n"
    "{{extend \"base\"}}\n"
    "\n"
    "Welcome. Edit this page, or add one with `ssg new about`.\n";

const char* kGitignore = "dist/\n";

// Today's date, honouring SOURCE_DATE_EPOCH like the build does.
std::string today() {
  std::time_t t = std::time(nullptr);
  if (const char* sde = std::getenv("SOURCE_DATE_EPOCH")) {
    char* end = nullptr;
    const long long v = std::strtoll(sde, &end, 10);
    if (end != sde && *end == '\0' && v >= 0 && v <= 253402300799LL) {
      t = static_cast<std::time_t>(v);
    }
  }
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[16];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
  return buf;
}

// "hello-world" -> "Hello world". Characters that would end a quoted value or open a tag are
// dropped.
std::string title_from(const std::string& stem) {
  std::string out;
  for (const char ch : stem) {
    if (ch == '"' || ch == '{' || ch == '}' || ch == '\\') continue;
    out += (ch == '-' || ch == '_') ? ' ' : ch;
  }
  if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out.empty() ? "Untitled" : out;
}

}  // namespace

std::vector<std::string> scaffold_site(const std::string& dir) {
  const fs::path root = dir;
  if (fs::exists(root / "site.toml")) {
    throw std::runtime_error(root.string() + "/site.toml already exists; not overwriting");
  }
  struct File {
    const char* name;
    const char* body;
  };
  const File files[] = {{"site.toml", kSiteToml},
                        {"skeleton.html", kSkeleton},
                        {"templates/base.md", kBaseTemplate},
                        {"index.md", kIndex},
                        {".gitignore", kGitignore}};
  std::vector<std::string> made;
  for (const File& f : files) {
    const fs::path p = root / f.name;
    // Don't clobber a page or skeleton that was already there.
    if (fs::exists(p)) {
      if (std::string(f.name) == ".gitignore") continue;
      throw std::runtime_error(p.string() + " already exists; not overwriting");
    }
    write_file(p, f.body);
    made.push_back(p.string());
  }
  return made;
}

std::string scaffold_page(const std::string& path) {
  fs::path p = path;
  if (p.is_absolute() || std::any_of(p.begin(), p.end(), [](const fs::path& c) { return c == ".."; })) {
    throw std::runtime_error("new wants a path inside the current directory, not " + path);
  }
  if (p.extension() != ".md") p += ".md";
  if (p.filename().string().empty() || p.filename() == ".md") {
    throw std::runtime_error("new needs a file name, not " + path);
  }
  if (fs::exists(fs::symlink_status(p))) {
    throw std::runtime_error(p.string() + " already exists; not overwriting");
  }
  write_file(p, "---\ntitle: \"" + title_from(p.stem().string()) + "\"\ndate: \"" + today() +
                    "\"\n---\n\n");
  return p.string();
}
