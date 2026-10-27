#pragma once

#include "config.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

// Data files (data/*.toml, data/*.json) flattened to dotted keys. `prefix` is the first part of
// every key ("data.site"). Objects and tables nest with ".", array items get their index
// ("data.site.tags.0") and the array gets "<key>.length". Booleans are "true"/"false", numbers
// their text, null the empty string. Throws std::runtime_error ("line 3: ...") on bad input;
// `skipped` receives keys that cannot be used in a template and so were left out.
void flatten_data(std::string_view text, bool is_json, const std::string& prefix,
                  std::map<std::string, std::string>& out, std::vector<std::string>& skipped);

struct Redirect {
  std::string from, to;
  int status = 301;
  int line = 0;
};
// Parses redirects.txt: `from to [status]` per line, `#` comments and blank lines ignored.
// Bad lines are skipped and described in `problems`.
std::vector<Redirect> parse_redirects(std::string_view text, std::vector<Diag>& problems);
// The `_redirects` file body (Netlify and Cloudflare Pages format).
std::string redirects_file(const std::vector<Redirect>& rs);
// Where a meta-refresh stub for `from` goes, relative to dist; "" if the rule has a wildcard.
std::string redirect_stub_path(const std::string& from);
std::string redirect_stub_html(const Redirect& r, const std::string& base_url);

// robots.txt for everyone: allow all, or disallow the listed paths. `sitemap_url` ("" for none)
// becomes a Sitemap: line.
std::string robots_txt(const RobotsConfig& cfg, const std::string& sitemap_url);

struct FeedEntry {
  std::string url;  // absolute
  std::string title, date, summary, author;
};
// Atom 1.0 with the entries in the order given. `updated` is a YYYY-MM-DD date. An entry without
// an author takes `author`; a feed with neither names its title as the author, since Atom wants one.
std::string feed_xml(const std::string& title, const std::string& author,
                     const std::string& site_url, const std::string& self_url,
                     const std::string& updated, const std::vector<FeedEntry>& entries);

// Lowercase; every run of characters other than ASCII letters, digits and non-ASCII bytes
// becomes one "-", with none at the ends. "C++ & Rust" gives "c-rust".
std::string slugify(std::string_view s);
