// ssg-plugin-images: asset. Writes resized (and optionally converted) copies of raster images
// next to the original: photos/cat.jpg gives photos/cat-480.webp, photos/cat-960.webp, ...
// It needs no library of its own: it runs ImageMagick (`magick`, or `convert`) when found, else
// macOS `sips`, which can resize but only writes jpeg and png. The original is left alone, and
// an image is never scaled up.
#include "plugin_main.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

const char* kMeta =
    "{\"name\":\"images\",\"version\":\"1.0.0\",\"protocol\":1,\"pure\":true,"
    "\"description\":\"Writes resized and converted copies of images (needs ImageMagick or sips)\","
    "\"hooks\":[\"init\",\"asset\"],\"config\":{"
    "\"widths\":{\"type\":\"array\",\"default\":[480,960]},"
    "\"format\":{\"type\":\"string\",\"default\":\"same\"},"
    "\"quality\":{\"type\":\"int\",\"default\":80},"
    "\"extensions\":{\"type\":\"array\",\"default\":[\"jpg\",\"jpeg\",\"png\"]}}}";

std::string quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

// Runs a shell command, returns its stdout; `ok` says whether it exited 0.
std::string run(const std::string& cmd, bool& ok) {
  std::string out;
  FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
  if (!p) {
    ok = false;
    return out;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
  ok = pclose(p) == 0;
  return out;
}

bool on_path(const std::string& tool) {
  bool ok;
  run("command -v " + quote(tool), ok);
  return ok;
}

enum class Tool { None, Magick, Convert, Sips };

Tool find_tool() {
  if (on_path("magick")) return Tool::Magick;
  if (on_path("convert")) return Tool::Convert;
  if (on_path("sips")) return Tool::Sips;
  return Tool::None;
}

long width_of(Tool t, const std::string& file) {
  bool ok;
  std::string out;
  if (t == Tool::Sips) {
    out = run("sips -g pixelWidth " + quote(file), ok);
    const size_t at = out.find("pixelWidth:");
    if (!ok || at == std::string::npos) return 0;
    return std::strtol(out.c_str() + at + 11, nullptr, 10);
  }
  out = run(std::string(t == Tool::Magick ? "magick identify" : "identify") + " -format %w " + quote(file + "[0]"), ok);
  return ok ? std::strtol(out.c_str(), nullptr, 10) : 0;
}

bool resize(Tool t, const std::string& in, const std::string& out, long width, long quality) {
  bool ok;
  switch (t) {
    case Tool::Magick:
    case Tool::Convert:
      run(std::string(t == Tool::Magick ? "magick " : "convert ") + quote(in) + " -resize " +
              std::to_string(width) + "x -quality " + std::to_string(quality) + " " + quote(out),
          ok);
      return ok;
    case Tool::Sips:
      run("sips --resampleWidth " + std::to_string(width) + " -s formatOptions " +
              std::to_string(quality) + " " + quote(in) + " --out " + quote(out),
          ok);
      return ok;
    case Tool::None: break;
  }
  return false;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

Tool g_tool = Tool::None;
bool g_warned = false;

Json handle(const std::string& hook, const Json& params, PluginContext& ctx) {
  if (hook == "init") {
    g_tool = find_tool();
    if (g_tool == Tool::None) {
      std::cerr << "no image tool found (install ImageMagick, or use macOS sips); no resized "
                   "images will be written\n";
      g_warned = true;
    }
    return Json("ok");
  }
  if (hook != "asset") throw std::runtime_error("unknown hook " + hook);
  Json files = Json::make_array();
  if (g_tool == Tool::None) return files;

  const std::string rel = params.str_at("path");
  const fs::path rp(rel);
  std::string ext = lower(rp.extension().string());
  if (!ext.empty()) ext.erase(0, 1);
  std::set<std::string> exts;
  if (const Json* e = ctx.config.find("extensions"); e && e->is_array()) {
    for (const Json& x : e->array) exts.insert(lower(x.str));
  }
  if (!exts.count(ext)) return files;

  std::string format = lower(ctx.config.str_at("format", "same"));
  if (format.empty() || format == "same") format = ext;
  if (g_tool == Tool::Sips && format != "jpg" && format != "jpeg" && format != "png") {
    if (!g_warned) {
      std::cerr << "sips can only write jpeg and png, not " << format << "; install ImageMagick\n";
      g_warned = true;
    }
    return files;
  }
  long quality = 80;
  if (const Json* q = ctx.config.find("quality"); q && q->kind == Json::Int) quality = q->integer;
  std::vector<long> widths;
  if (const Json* w = ctx.config.find("widths"); w && w->is_array()) {
    for (const Json& x : w->array) {
      if (x.kind == Json::Int && x.integer > 0) widths.push_back(static_cast<long>(x.integer));
    }
  }
  std::sort(widths.begin(), widths.end());
  widths.erase(std::unique(widths.begin(), widths.end()), widths.end());

  const std::string source = params.str_at("source");
  const long have = width_of(g_tool, source);
  if (have <= 0) {
    std::cerr << rel << ": cannot read the image size; skipped\n";
    return files;
  }
  const std::string stem = rp.stem().string();
  const std::string dir = rp.parent_path().generic_string();
  for (long w : widths) {
    if (w >= have) continue;  // never scale up
    const std::string name = stem + "-" + std::to_string(w) + "." + format;
    // The scratch name carries a hash of the full path, so same-named images can't collide.
    const std::string scratch =
        std::to_string(std::hash<std::string>{}(rel)) + "-" + name;
    const std::string out = (fs::path(params.str_at("out_dir")) / scratch).string();
    if (!resize(g_tool, source, out, w, quality) || !fs::is_regular_file(out)) {
      std::cerr << rel << ": could not write a " << w << "px copy\n";
      continue;
    }
    Json f = Json::make_object();
    f.set("path", Json(dir.empty() ? name : dir + "/" + name)).set("file", Json(scratch));
    files.push(f);
  }
  return files;
}

}  // namespace

int main(int argc, char** argv) { return plugin_main(argc, argv, kMeta, handle); }
