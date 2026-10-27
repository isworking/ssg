#pragma once

#include "config.hpp"
#include "json.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Plugin protocol 1 (frozen from rc.1). A plugin is an executable `ssg-plugin-<name>`
// that ssg starts once per build and talks to in JSON lines; see docs/plugins.md.
constexpr int kPluginProtocol = 1;

// One [plugin.<name>] table: the keys every plugin has, and the rest, which is its config.
struct PluginSpec {
  std::string name;
  bool enabled = true;
  bool required = true;
  int timeout = 30;      // seconds, for each request
  std::string path;      // explicit path = "...", else looked up on PATH
  Json config = Json::make_object();
};

// What `ssg-plugin-<name> --meta` printed.
struct PluginMeta {
  std::string name, version, description;
  int protocol = 0;
  bool pure = false;
  std::vector<std::string> hooks;
  Json config = Json::make_object();  // key -> {"type": ..., "default": ...}
};

// The tables of site.toml in file order. Throws std::runtime_error on a bad common key.
std::vector<PluginSpec> plugin_specs(const SiteConfig& cfg);
// Where the plugin's executable is; empty, with `why` set, if it isn't found.
std::filesystem::path find_plugin(const SiteConfig& cfg, const PluginSpec& spec, std::string& why);
// Runs `exe --meta`. Throws std::runtime_error naming the problem.
PluginMeta read_plugin_meta(const std::filesystem::path& exe, int timeout, std::string* raw = nullptr);
// Warnings about config keys the plugin doesn't declare; throws if a value has the wrong type.
// Returns the config with the plugin's defaults filled in.
Json check_plugin_config(const PluginSpec& spec, const PluginMeta& meta,
                         std::vector<std::string>& warnings);

struct PluginInfo {
  std::string name, version;
  int protocol = 1;
  std::filesystem::path exe;
  std::vector<std::string> hooks;
};

struct PluginFile {
  std::string path;            // where it is published, relative to dist
  std::filesystem::path file;  // where the plugin wrote it
};

// The running plugins of one build.
class PluginHost {
 public:
  using Warn = std::function<void(const std::string&)>;
  PluginHost(const SiteConfig& cfg, Warn warn, bool announce);
  ~PluginHost();
  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  // Finds, checks, starts and initialises every enabled plugin. Throws if a required one fails.
  // `info` is the site description sent with `init`.
  void start(const Json& site);
  const std::vector<PluginInfo>& active() const { return infos_; }
  bool empty() const { return infos_.empty(); }
  bool wants(const std::string& hook) const;

  // Passes params[field] through every plugin that declares `hook`, in order.
  std::string transform(const std::string& hook, Json params, const std::string& field,
                        std::string value);
  // The first plugin declaring container:<name> turns the body into Markdown. False if none does.
  bool container(const std::string& name, const Json& params, std::string& markdown);
  std::vector<PluginFile> assets(const std::string& rel, const std::filesystem::path& source,
                                 const std::filesystem::path& out_dir);
  void post_build(const std::filesystem::path& manifest, const std::filesystem::path& dist);
  // Closes every plugin; a required one that fails to exit cleanly fails the build.
  void stop();

 private:
  struct Running;
  const SiteConfig& cfg_;
  Warn warn_;
  bool announce_;
  std::vector<std::unique_ptr<Running>> run_;
  std::vector<PluginInfo> infos_;
  Json call(Running& r, const std::string& hook, const Json& params, bool& ok);
  // A problem with one plugin: fatal if it is required, else a warning and the plugin is dropped.
  void fail(Running& r, const std::string& hook, const std::string& what);
};
