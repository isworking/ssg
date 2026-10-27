#pragma once

#include <string>
#include <vector>

// ssg init: writes a minimal site into `dir` (created if needed) and returns the files made,
// as paths under `dir`. Throws if dir already has a site.toml.
std::vector<std::string> scaffold_site(const std::string& dir);

// ssg new: writes a page with title and date frontmatter and returns its path. ".md" is added
// if missing. Throws if the file exists.
std::string scaffold_page(const std::string& path);
