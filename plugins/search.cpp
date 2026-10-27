// ssg-plugin-search: post_build. Writes a JSON search index from manifest.json and the .txt
// output of every page: {"version": 1, "pages": [{"url", "title", "date", "text"}]}.
#include "plugin_main.hpp"
#include "util.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

const char* kMeta =
    "{\"name\":\"search\",\"version\":\"1.0.0\",\"protocol\":1,\"pure\":true,"
    "\"description\":\"Writes a JSON search index of the site's pages\","
    "\"hooks\":[\"init\",\"post_build\"],\"config\":{"
    "\"output\":{\"type\":\"string\",\"default\":\"search-index.json\"},"
    "\"max_chars\":{\"type\":\"int\",\"default\":5000},"
    "\"exclude\":{\"type\":\"array\",\"default\":[\"/404/\"]}}}";

// Whitespace runs become one space; cut at max bytes without splitting a UTF-8 character.
std::string squeeze(const std::string& s, size_t max) {
  std::string out;
  bool space = false;
  for (unsigned char c : s) {
    if (std::isspace(c)) {
      space = !out.empty();
      continue;
    }
    if (space) out += ' ';
    space = false;
    out += static_cast<char>(c);
  }
  if (max && out.size() > max) {
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

Json handle(const std::string& hook, const Json& params, PluginContext& ctx) {
  if (hook == "init") return Json("ok");
  if (hook != "post_build") throw std::runtime_error("unknown hook " + hook);
  const fs::path dist = params.str_at("dist");
  const Json manifest = parse_json(read_file(params.str_at("manifest")));
  const Json* pages = manifest.find("pages");
  if (!pages || !pages->is_array()) throw std::runtime_error("manifest.json has no pages");
  const std::string output = ctx.config.str_at("output", "search-index.json");
  const fs::path rel = fs::path(output).lexically_normal();
  if (output.empty() || rel.is_absolute() || *rel.begin() == "..") {
    throw std::runtime_error("output must be a path inside dist, not '" + output + "'");
  }
  size_t max = 5000;
  if (const Json* m = ctx.config.find("max_chars"); m && m->kind == Json::Int && m->integer >= 0) {
    max = static_cast<size_t>(m->integer);
  }
  std::vector<std::string> exclude;
  if (const Json* x = ctx.config.find("exclude"); x && x->is_array()) {
    for (const Json& e : x->array) exclude.push_back(e.str);
  }
  std::string out = "{\"version\":1,\"pages\":[";
  bool first = true;
  for (const Json& page : pages->array) {
    const std::string url = page.str_at("url");
    if (std::find(exclude.begin(), exclude.end(), url) != exclude.end()) continue;
    std::string text;
    if (const Json* outs = page.find("outputs")) {
      for (const Json& o : outs->array) {
        if (o.str_at("format") == "txt") text = read_file(dist / o.str_at("path"));
      }
    }
    out += std::string(first ? "" : ",") + "\n{\"url\":" + json_str(url) +
           ",\"title\":" + json_str(page.str_at("title")) +
           ",\"date\":" + json_str(page.str_at("date")) + ",\"text\":" + json_str(squeeze(text, max)) + "}";
    first = false;
  }
  out += "\n]}\n";
  write_file(dist / rel, out);
  return Json("ok");
}

}  // namespace

int main(int argc, char** argv) { return plugin_main(argc, argv, kMeta, handle); }
