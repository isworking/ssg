#pragma once

#include <string>
#include <string_view>

// Conservative minifier: drops comments (except /*! ... */), collapses whitespace, and trims it
// around { } ; , and after : ( and before ). Strings, url(...) and escapes are copied as
// written, and nothing around + - * / is touched, so calc() keeps its spaces.
std::string minify_css(std::string_view css);

// SHA-256 of `data`: lowercase hex, and base64 (what a CSP 'sha256-...' source wants).
std::string sha256_hex(std::string_view data);
std::string sha256_base64(std::string_view data);
