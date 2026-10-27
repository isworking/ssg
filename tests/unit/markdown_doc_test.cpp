// MarkdownDoc parses once and renders any number of times, with the same result as the one-shot
// functions.
#include "markdown.hpp"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
  if (ok) return;
  std::cerr << "FAIL: " << what << "\n";
  ++failures;
}
}  // namespace

int main() {
  const std::string md =
      "# Title\n\nSome *text* with a note.[^1]\n\n## Two\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
      "## Two\n\n- [ ] task\n\n[^1]: The note.\n";
  const MarkdownOptions opt;
  const MarkdownDoc doc(md, opt);
  const std::string html = markdown_to_html(md, opt);
  const std::string text = markdown_to_text(md, opt);
  check(doc.html() == html, "html matches markdown_to_html");
  check(doc.text() == text, "text matches markdown_to_text");
  // Rendering must not consume or alter the tree.
  check(doc.html() == html, "html is repeatable");
  check(doc.text() == text, "text is repeatable");
  check(doc.html() == html, "html after text");
  const auto hs = doc.headings();
  const auto want = markdown_headings(md, opt);
  check(hs.size() == 3 && want.size() == 3, "three headings");
  for (size_t i = 0; i < hs.size() && i < want.size(); ++i) {
    check(hs[i].level == want[i].level && hs[i].text == want[i].text && hs[i].id == want[i].id,
          "heading " + std::to_string(i));
  }
  check(hs.size() == 3 && hs[2].id == "two-1", "duplicate heading ids are deduplicated");
  MarkdownDoc first("# x\n", opt);
  MarkdownDoc moved(std::move(first));
  check(moved.html() == "<h1 id=\"x\">x</h1>\n", "moved-to doc renders");
  return failures ? 1 : 0;
}
