#include "build.hpp"
#include "config.hpp"
#include "markdown.hpp"
#include "util.hpp"

#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

const char* kVersion = "0.2.0";

const char* kUsage =
    "usage: ssg [--drafts] [--check-links] [--quiet] <content-dir>\n"
    "       ssg render [--strict] [--text] [--no-html] [--scripts|--no-scripts]\n"
    "                  [--no-extensions] <file|->\n"
    "       ssg --help | --version\n";

const char* kHelp =
    "Build <content-dir> (it must contain site.toml) into its dist dir.\n"
    "\n"
    "  --drafts         also build pages with draft: \"true\"\n"
    "  --check-links    warn about local href/src targets missing from dist\n"
    "  --quiet          no summary line\n"
    "\n"
    "render: markdown on stdout, no frontmatter or templates.\n"
    "  --strict         CommonMark only: raw HTML and scripts pass through, no extensions,\n"
    "                   heading ids, or bare autolinks\n"
    "  --text           plain text instead of HTML\n"
    "  --no-html        escape raw HTML\n"
    "  --scripts        let <script>, on* attributes and javascript: URLs through\n"
    "  --no-scripts     strip them (the default)\n"
    "  --no-extensions  strict CommonMark syntax\n";

int usage_error(const std::string& msg) {
  if (!msg.empty()) std::cerr << "ssg: " << msg << "\n";
  std::cerr << kUsage;
  return 2;
}

int render_cmd(int argc, char** argv) {
  MarkdownOptions opt;
  bool text = false;
  std::string input;
  bool have_input = false;
  for (int i = 2; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--strict") {
      opt.raw_html = true;
      opt.scripts = true;
      opt.extensions = false;
      opt.heading_ids = false;
      opt.autolinks = false;
    } else if (a == "--text") {
      text = true;
    } else if (a == "--no-html") {
      opt.raw_html = false;
    } else if (a == "--scripts") {
      opt.scripts = true;
    } else if (a == "--no-scripts") {
      opt.scripts = false;
    } else if (a == "--no-extensions") {
      opt.extensions = false;
    } else if (a.size() > 1 && a[0] == '-') {
      return usage_error("unknown option " + std::string(a));
    } else if (have_input) {
      return usage_error("render takes one input");
    } else {
      input = std::string(a);
      have_input = true;
    }
  }
  if (!have_input) return usage_error("render needs a file or -");

  std::string md;
  if (input == "-") {
    md.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
  } else {
    md = read_file(input);
  }
  std::cout << (text ? markdown_to_text(md, opt) : markdown_to_html(md, opt));
  std::cout.flush();
  if (!std::cout) {
    std::cerr << "ssg: error writing output\n";
    return 1;
  }
  return 0;
}

int build_cmd(int argc, char** argv) {
  BuildOptions opt;
  bool drafts = false;
  std::string dir;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--drafts") drafts = true;
    else if (a == "--check-links") opt.check_links = true;
    else if (a == "--quiet" || a == "-q") opt.quiet = true;
    else if (a.size() > 1 && a[0] == '-') return usage_error("unknown option " + std::string(a));
    else if (!dir.empty()) return usage_error("one content dir only");
    else dir = std::string(a);
  }
  if (dir.empty()) return usage_error("");
  SiteConfig cfg = load_site_config(dir);
  cfg.drafts = drafts;
  build_site(cfg, opt);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  if (argc >= 2) {
    const std::string_view a = argv[1];
    if (a == "--help" || a == "-h") {
      std::cout << kUsage << "\n" << kHelp;
      return 0;
    }
    if (a == "--version") {
      std::cout << "ssg " << kVersion << "\n";
      return 0;
    }
  }
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "render") return render_cmd(argc, argv);
    return build_cmd(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "ssg: " << e.what() << "\n";
    return 1;
  }
}
