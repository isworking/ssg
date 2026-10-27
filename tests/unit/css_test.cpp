// The CSS minifier against golden outputs, and SHA-256 against known vectors.
// usage: css_test CORPUS-DIR   (X.css is minified and compared with X.min.css)
#include "css.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (ok) return;
  std::cerr << "FAIL: " << what << "\n";
  ++failures;
}

std::string slurp(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

int main(int argc, char** argv) {
  check(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 of nothing");
  check(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 of abc");
  check(sha256_hex(std::string(1000, 'a')) ==
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
        "sha256 of a multi-block message");
  check(sha256_base64("abc") == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=", "sha256 base64");

  const auto gather = [](const std::string& css) { return minify_css(css); };
  check(gather("a { color : red ; }") == "a{color :red}", "basic collapse");
  check(gather("a/**/b{}") == "a b{}", "a comment separates tokens");
  check(gather("calc(1px + 2px)") == "calc(1px + 2px)", "calc spaces kept");
  check(gather("a{b:\"x  y\"}") == "a{b:\"x  y\"}", "strings kept");
  check(gather("a{b:url(  x  y )}") == "a{b:url(x  y)}", "unquoted url kept");
  check(gather("/*! k */a{}") == "/*! k */a{}", "bang comments kept");
  check(gather("a{b:c\\;}") == "a{b:c\\;}", "an escaped ; is not dropped");

  if (argc >= 2) {
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(argv[1])) {
      const auto p = e.path();
      const std::string name = p.filename().string();
      if (p.extension() != ".css" || (name.size() > 8 && name.compare(name.size() - 8, 8, ".min.css") == 0)) {
        continue;
      }
      ++n;
      const std::string want = slurp(p.parent_path() / (p.stem().string() + ".min.css"));
      const std::string got = minify_css(slurp(p)) + "\n";
      check(got == want, name + " does not match its .min.css golden");
      check(minify_css(got) + "\n" == got, name + ": minifying twice changed the output");
    }
    check(n >= 3, "corpus has at least 3 stylesheets");
  }
  if (failures == 0) std::cout << "css_test: ok\n";
  return failures ? 1 : 0;
}
