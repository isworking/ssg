#pragma once

#include <bitset>
#include <string>
#include <string_view>
#include <vector>

// One glob, compiled. git's wildmatch rules (WM_PATHNAME): '*' and '?' don't cross '/',
// [a-z] / [!a-z] / [^a-z] / [[:alpha:]] classes, '\' escapes, and "**/" / "/**" / "/**/" match
// across directories. Matching is O(pattern * path), with no backtracking.
class Glob {
 public:
  Glob() = default;
  explicit Glob(std::string_view pattern);
  bool match(std::string_view path) const;

 private:
  enum class Kind : unsigned char { Lit, Any, Set, Star, StarSlash, StarDirs };
  struct Tok {
    Kind kind = Kind::Lit;
    char c = 0;       // Lit
    size_t set = 0;   // Set: index into sets_
  };
  std::vector<Tok> toks_;
  std::vector<std::bitset<256>> sets_;
  bool never_ = false;  // malformed (unterminated class, unknown [:name:], trailing '\')
};

// gitignore-style rules. Paths are relative to the content dir, '/'-separated, no leading or
// trailing slash. Callers walk top-down and skip ignored directories, so (as in git) a file
// can't be re-included when a parent directory is excluded.
class IgnoreRules {
 public:
  // One pattern line from a file in directory `base` ("" = content root).
  void add(std::string_view base, std::string_view line);
  // Every line of a .assetignore-style file.
  void add_file(std::string_view base, std::string_view text);
  bool ignored(std::string_view rel, bool is_dir) const;

 private:
  struct Rule {
    std::string base;  // dir the rule came from, "" or "a/b"
    Glob glob;         // one leading '/' and one trailing '/' stripped
    bool negate = false;
    bool dir_only = false;
    bool anchored = false;  // had a '/' at the start or middle: match the whole path
  };
  std::vector<Rule> rules_;
};

bool glob_match(std::string_view pattern, std::string_view path);
