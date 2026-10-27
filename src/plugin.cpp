#include "plugin.hpp"

#include "toml.hpp"
#include "util.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <set>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr size_t kMaxLine = 256u << 20;  // one reply; pages are small, this is a safety stop
constexpr int kStopGraceMs = 5000;

bool name_ok(const std::string& n) {
  return !n.empty() && std::all_of(n.begin(), n.end(), [](char c) {
           return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
         });
}

bool inside(const fs::path& dir, const fs::path& p) {
  std::error_code ec;
  const fs::path d = fs::weakly_canonical(dir, ec), q = fs::weakly_canonical(p, ec);
  auto di = d.begin(), qi = q.begin();
  for (; di != d.end(); ++di, ++qi) {
    if (qi == q.end() || *di != *qi) return false;
  }
  return true;
}

bool executable_file(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec) && access(p.c_str(), X_OK) == 0;
}

Json toml_to_json(const TomlValue& v) {
  switch (v.kind) {
    case TomlValue::String: return Json(v.str);
    case TomlValue::Bool: return Json(v.boolean);
    case TomlValue::Int: return Json(v.integer);
    case TomlValue::Array: {
      Json a = Json::make_array();
      for (const auto& e : v.array) a.push(toml_to_json(e));
      return a;
    }
  }
  return Json();
}

const char* kind_name(const Json& j) {
  switch (j.kind) {
    case Json::Null: return "null";
    case Json::Bool: return "bool";
    case Json::Int: return "int";
    case Json::Real: return "number";
    case Json::String: return "string";
    case Json::Array: return "array";
    case Json::Object: return "object";
  }
  return "?";
}

std::string describe_status(int st) {
  if (WIFEXITED(st)) return "exited with status " + std::to_string(WEXITSTATUS(st));
  if (WIFSIGNALED(st)) return "was killed by signal " + std::to_string(WTERMSIG(st));
  return "stopped";
}

// A child process with pipes to its stdin, stdout and stderr.
class Proc {
 public:
  using LineFn = std::function<void(const std::string&)>;
  Proc() = default;
  Proc(const Proc&) = delete;
  Proc& operator=(const Proc&) = delete;
  ~Proc() { kill_now(); }

  void spawn(const fs::path& exe, const std::vector<std::string>& args,
             const std::vector<std::pair<std::string, std::string>>& env, LineFn on_err) {
    on_err_ = std::move(on_err);
    std::signal(SIGPIPE, SIG_IGN);
    int in[2], out[2], err[2];
    if (pipe(in) || pipe(out) || pipe(err)) throw std::runtime_error("cannot create pipes");
    for (int fd : {in[1], out[0], err[0]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
    // Everything the child needs is built before fork: no allocation in between.
    const std::string exe_s = exe.string();
    std::vector<std::string> argv_s{exe_s};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : argv_s) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::vector<std::string> env_s;
    for (char** e = environ; e && *e; ++e) {
      bool replaced = false;
      for (const auto& kv : env) replaced = replaced || starts_with(*e, kv.first + "=");
      if (!replaced) env_s.push_back(*e);
    }
    for (const auto& kv : env) env_s.push_back(kv.first + "=" + kv.second);
    std::vector<char*> envp;
    for (auto& e : env_s) envp.push_back(e.data());
    envp.push_back(nullptr);

    pid_ = fork();
    if (pid_ < 0) throw std::runtime_error(std::string("cannot start a process: ") + std::strerror(errno));
    if (pid_ == 0) {
      dup2(in[0], 0);
      dup2(out[1], 1);
      dup2(err[1], 2);
      execve(exe_s.c_str(), argv.data(), envp.data());
      _exit(127);
    }
    close(in[0]);
    close(out[1]);
    close(err[1]);
    in_ = in[1];
    out_ = out[0];
    err_ = err[0];
    for (int fd : {in_, out_, err_}) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  }

  void close_stdin() {
    if (in_ >= 0) close(in_);
    in_ = -1;
  }

  void write_line(const std::string& s, Clock::time_point deadline) {
    const std::string data = s + "\n";
    size_t off = 0;
    while (off < data.size()) {
      if (in_ < 0) throw std::runtime_error("the plugin closed its input");
      const bool writable = pump(remaining(deadline), true);
      if (expired(deadline) && !writable) throw Timeout();
      if (!writable) continue;
      const ssize_t n = ::write(in_, data.data() + off, data.size() - off);
      if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) continue;
        throw std::runtime_error("the plugin closed its input" + status_suffix());
      }
      off += static_cast<size_t>(n);
    }
  }

  struct Timeout {};

  // One line of stdout. False at end of output.
  bool read_line(std::string& line, Clock::time_point deadline) {
    while (true) {
      const size_t nl = outbuf_.find('\n');
      if (nl != std::string::npos) {
        line = outbuf_.substr(0, nl);
        outbuf_.erase(0, nl + 1);
        return true;
      }
      if (out_ < 0) {
        if (outbuf_.empty()) return false;
        line.swap(outbuf_);
        outbuf_.clear();
        return true;
      }
      if (outbuf_.size() > kMaxLine) throw std::runtime_error("reply is too long");
      pump(remaining(deadline), false);
      if (expired(deadline) && outbuf_.find('\n') == std::string::npos && out_ >= 0) throw Timeout();
    }
  }

  std::string read_all(Clock::time_point deadline) {
    while (out_ >= 0) {
      if (outbuf_.size() > (1u << 20)) throw std::runtime_error("output is too long");
      pump(remaining(deadline), false);
      if (expired(deadline) && out_ >= 0) throw Timeout();
    }
    return std::move(outbuf_);
  }

  // Waits up to `ms` for the child to exit, draining its output meanwhile. -1 if it hasn't.
  bool wait_exit(int ms, int& status) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    while (true) {
      if (reaped_) {
        status = status_;
        return true;
      }
      if (try_reap()) {
        drain();
        status = status_;
        return true;
      }
      if (Clock::now() >= deadline) return false;
      pump(20, false);
      if (out_ < 0 && err_ < 0) usleep(5000);
    }
  }

  void kill_now() {
    if (pid_ > 0 && !reaped_) {
      ::kill(pid_, SIGKILL);
      int st;
      waitpid(pid_, &st, 0);
      reaped_ = true;
    }
    close_stdin();
    for (int* fd : {&out_, &err_}) {
      if (*fd >= 0) close(*fd);
      *fd = -1;
    }
    flush_err();
  }

  std::string status_suffix() {
    int st;
    if (wait_exit(200, st)) return "; it " + describe_status(st);
    return "";
  }

 private:
  pid_t pid_ = -1;
  int in_ = -1, out_ = -1, err_ = -1;
  std::string outbuf_, errbuf_;
  bool reaped_ = false;
  int status_ = 0;
  LineFn on_err_;

  static int remaining(Clock::time_point d) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d - Clock::now()).count();
    return ms < 0 ? 0 : static_cast<int>(std::min<long long>(ms, 1000));
  }
  static bool expired(Clock::time_point d) { return Clock::now() >= d; }

  bool try_reap() {
    int st;
    if (waitpid(pid_, &st, WNOHANG) == pid_) {
      reaped_ = true;
      status_ = st;
      return true;
    }
    return false;
  }

  void flush_err() {
    while (!errbuf_.empty()) {
      const size_t nl = errbuf_.find('\n');
      const std::string l = errbuf_.substr(0, nl);
      errbuf_.erase(0, nl == std::string::npos ? errbuf_.size() : nl + 1);
      if (on_err_ && !trim(l).empty()) on_err_(std::string(trim(l)));
    }
  }

  void take(int& fd, std::string& into) {
    char buf[65536];
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n > 0) {
      into.append(buf, static_cast<size_t>(n));
    } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
      close(fd);
      fd = -1;
    }
  }

  void drain() {
    for (int i = 0; i < 4 && (out_ >= 0 || err_ >= 0); ++i) pump(0, false);
  }

  // Polls the pipes for up to `ms`, buffering whatever arrives. True if stdin can take bytes.
  bool pump(int ms, bool want_write) {
    pollfd fds[3];
    int n = 0, wi = -1, oi = -1, ei = -1;
    if (want_write && in_ >= 0) {
      wi = n;
      fds[n++] = {in_, POLLOUT, 0};
    }
    if (out_ >= 0) {
      oi = n;
      fds[n++] = {out_, POLLIN, 0};
    }
    if (err_ >= 0) {
      ei = n;
      fds[n++] = {err_, POLLIN, 0};
    }
    if (n == 0) return false;
    const int r = poll(fds, static_cast<nfds_t>(n), ms);
    if (r <= 0) return false;
    if (oi >= 0 && fds[oi].revents) take(out_, outbuf_);
    if (ei >= 0 && fds[ei].revents) {
      take(err_, errbuf_);
      size_t nl;
      while ((nl = errbuf_.find('\n')) != std::string::npos) {
        const std::string l = errbuf_.substr(0, nl);
        errbuf_.erase(0, nl + 1);
        if (on_err_ && !trim(l).empty()) on_err_(std::string(trim(l)));
      }
    }
    return wi >= 0 && (fds[wi].revents & (POLLOUT | POLLERR | POLLHUP));
  }
};

}  // namespace

std::vector<PluginSpec> plugin_specs(const SiteConfig& cfg) {
  std::vector<PluginSpec> out;
  for (const std::string& name : cfg.plugin_order) {
    PluginSpec s;
    s.name = name;
    const std::string tag = "[plugin." + toml_path_string({name}) + "]";
    if (!name_ok(name)) {
      throw std::runtime_error(tag + ": a plugin name uses only letters, digits, - and _");
    }
    for (const TomlEntry& e : cfg.plugins.at(name)) {
      const TomlValue& v = e.value;
      const std::string& k = e.path[0];
      if (e.path.size() == 1 && k == "enabled") {
        if (v.kind != TomlValue::Bool) throw std::runtime_error(tag + " enabled must be true or false");
        s.enabled = v.boolean;
      } else if (e.path.size() == 1 && k == "required") {
        if (v.kind != TomlValue::Bool) throw std::runtime_error(tag + " required must be true or false");
        s.required = v.boolean;
      } else if (e.path.size() == 1 && k == "path") {
        if (v.kind != TomlValue::String || v.str.empty()) {
          throw std::runtime_error(tag + " path must be a non-empty string");
        }
        s.path = v.str;
      } else if (e.path.size() == 1 && k == "timeout") {
        if (v.kind != TomlValue::Int || v.integer < 1 || v.integer > 86400) {
          throw std::runtime_error(tag + " timeout must be a whole number of seconds, 1 to 86400");
        }
        s.timeout = static_cast<int>(v.integer);
      } else {
        Json* at = &s.config;
        for (size_t i = 0; i + 1 < e.path.size(); ++i) {
          Json& next = at->object[e.path[i]];
          if (next.kind != Json::Object) next = Json::make_object();
          at = &next;
        }
        at->object[e.path.back()] = toml_to_json(v);
      }
    }
    out.push_back(std::move(s));
  }
  return out;
}

fs::path find_plugin(const SiteConfig& cfg, const PluginSpec& spec, std::string& why) {
  if (!spec.path.empty()) {
    fs::path p = spec.path;
    if (p.is_relative()) p = cfg.content_dir / p;
    if (!executable_file(p)) {
      why = p.string() + " is not an executable file";
      return {};
    }
    return p;
  }
  const std::string exe = "ssg-plugin-" + spec.name;
  const char* path = std::getenv("PATH");
  std::string rest = path ? path : "";
  size_t pos = 0;
  while (pos <= rest.size()) {
    size_t colon = rest.find(':', pos);
    if (colon == std::string::npos) colon = rest.size();
    const std::string dir = rest.substr(pos, colon - pos);
    pos = colon + 1;
    // An empty or relative entry means the current directory, which may be the site.
    if (dir.empty() || dir[0] != '/') continue;
    if (inside(cfg.content_dir, dir)) continue;
    const fs::path cand = fs::path(dir) / exe;
    if (executable_file(cand)) return cand;
  }
  why = exe + " is not on PATH (outside the content dir); install it or set path = \"...\"";
  return {};
}

PluginMeta read_plugin_meta(const fs::path& exe, int timeout, std::string* raw) {
  Proc p;
  p.spawn(exe, {"--meta"}, {{"SSG_PROTOCOL", std::to_string(kPluginProtocol)}}, nullptr);
  p.close_stdin();
  std::string text;
  try {
    text = p.read_all(Clock::now() + std::chrono::seconds(timeout));
  } catch (const Proc::Timeout&) {
    throw std::runtime_error("--meta timed out after " + std::to_string(timeout) + "s");
  }
  int st = 0;
  p.wait_exit(2000, st);
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    throw std::runtime_error("--meta " + describe_status(st));
  }
  Json j;
  try {
    j = parse_json(text);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("--meta did not print JSON: ") + e.what());
  }
  if (!j.is_object()) throw std::runtime_error("--meta must print one JSON object");
  PluginMeta m;
  m.name = j.str_at("name");
  m.version = j.str_at("version");
  m.description = j.str_at("description");
  if (m.name.empty() || m.version.empty()) {
    throw std::runtime_error("--meta needs \"name\" and \"version\"");
  }
  const Json* proto = j.find("protocol");
  if (!proto || proto->kind != Json::Int || proto->integer < 1) {
    throw std::runtime_error("--meta needs \"protocol\" (a whole number)");
  }
  m.protocol = static_cast<int>(std::min<long long>(proto->integer, 1 << 20));
  if (const Json* pure = j.find("pure"); pure && pure->kind == Json::Bool) m.pure = pure->boolean;
  if (const Json* h = j.find("hooks"); h && h->is_array()) {
    for (const Json& e : h->array) {
      if (e.is_string()) m.hooks.push_back(e.str);
    }
  }
  if (const Json* c = j.find("config"); c && c->is_object()) m.config = *c;
  if (raw) *raw = j.dump();
  return m;
}

Json check_plugin_config(const PluginSpec& spec, const PluginMeta& meta,
                         std::vector<std::string>& warnings) {
  const std::string tag = "[plugin." + spec.name + "]";
  Json out = spec.config;
  for (const auto& kv : spec.config.object) {
    const auto decl = meta.config.object.find(kv.first);
    if (decl == meta.config.object.end()) {
      warnings.push_back(tag + " has the key '" + kv.first + "', which " + meta.name +
                         " does not declare");
      continue;
    }
    const Json& d = decl->second;
    const std::string type = d.is_string() ? d.str : d.str_at("type");
    if (type.empty() || type == "any") continue;
    const char* got = kind_name(kv.second);
    const bool ok = type == got || (type == "number" && kv.second.kind == Json::Int);
    if (!ok) {
      throw std::runtime_error(tag + " '" + kv.first + "' must be " + type + ", not " + got);
    }
  }
  for (const auto& kv : meta.config.object) {
    if (spec.config.find(kv.first)) continue;
    if (const Json* def = kv.second.find("default")) out.object[kv.first] = *def;
  }
  return out;
}

struct PluginHost::Running {
  PluginSpec spec;
  PluginInfo info;
  Json config;
  Proc proc;
  long long next_id = 0;
  bool dead = false;
  bool has(const std::string& hook) const {
    return !dead && std::find(info.hooks.begin(), info.hooks.end(), hook) != info.hooks.end();
  }
};

PluginHost::PluginHost(const SiteConfig& cfg, Warn warn, bool announce)
    : cfg_(cfg), warn_(std::move(warn)), announce_(announce) {}

PluginHost::~PluginHost() = default;

void PluginHost::fail(Running& r, const std::string& hook, const std::string& what) {
  const std::string msg = "plugin '" + r.spec.name + "'" +
                          (hook.empty() ? "" : ", hook '" + hook + "'") + ": " + what;
  if (r.spec.required) throw std::runtime_error(msg);
  warn_(msg + " (required = false: plugin skipped)");
  r.dead = true;
  r.proc.kill_now();
}

bool PluginHost::wants(const std::string& hook) const {
  for (const auto& r : run_) {
    if (r->has(hook)) return true;
  }
  return false;
}

Json PluginHost::call(Running& r, const std::string& hook, const Json& params, bool& ok) {
  ok = false;
  try {
    const auto deadline = Clock::now() + std::chrono::seconds(r.spec.timeout);
    const long long id = ++r.next_id;
    Json req = Json::make_object();
    req.set("id", Json(id)).set("hook", Json(hook)).set("params", params);
    r.proc.write_line(req.dump(), deadline);
    std::string line;
    while (true) {
      if (!r.proc.read_line(line, deadline)) {
        throw std::runtime_error("exited before answering" + r.proc.status_suffix());
      }
      if (!trim(line).empty()) break;
    }
    Json res;
    try {
      res = parse_json(line);
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("answered with invalid JSON (") + e.what() + ")");
    }
    const Json* rid = res.find("id");
    if (!res.is_object() || !rid || rid->kind != Json::Int || rid->integer != id) {
      throw std::runtime_error("answered with the wrong id (wanted " + std::to_string(id) + ")");
    }
    if (const Json* err = res.find("error")) {
      throw std::runtime_error("failed: " + (err->is_string() ? err->str : err->dump()));
    }
    const Json* result = res.find("result");
    if (!result) throw std::runtime_error("answered without a result");
    ok = true;
    return *result;
  } catch (const Proc::Timeout&) {
    fail(r, hook, "timed out after " + std::to_string(r.spec.timeout) + "s");
  } catch (const std::exception& e) {
    fail(r, hook, e.what());
  }
  return Json();
}

void PluginHost::start(const Json& site) {
  const std::string date = site.str_at("build_date");
  for (PluginSpec& spec : plugin_specs(cfg_)) {
    if (!spec.enabled) continue;
    auto r = std::make_unique<Running>();
    r->spec = spec;
    // Setup problems are reported like any other failure of that plugin.
    try {
      std::string why;
      const fs::path exe = find_plugin(cfg_, spec, why);
      if (exe.empty()) throw std::runtime_error(why);
      PluginMeta meta;
      try {
        meta = read_plugin_meta(exe, spec.timeout);
      } catch (const std::exception& e) {
        throw std::runtime_error(std::string(e.what()) + " (" + exe.string() + ")");
      }
      if (meta.protocol > kPluginProtocol) {
        throw std::runtime_error("needs plugin protocol " + std::to_string(meta.protocol) +
                                 ", but this ssg speaks " + std::to_string(kPluginProtocol));
      }
      if (meta.name != spec.name) {
        throw std::runtime_error("--meta names the plugin '" + meta.name + "', not '" + spec.name + "'");
      }
      std::vector<std::string> notes;
      r->config = check_plugin_config(spec, meta, notes);
      for (const auto& n : notes) warn_(n);
      for (const auto& h : meta.hooks) {
        static const std::set<std::string> known{"init", "transform_source", "post_page", "asset",
                                                 "post_build"};
        if (!known.count(h) && !starts_with(h, "container:")) {
          warn_("plugin '" + spec.name + "' declares the hook '" + h + "', which this ssg does not know");
        }
      }
      r->info = {meta.name, meta.version, meta.protocol, exe, meta.hooks};
      if (announce_) {
        std::cerr << "ssg: plugin " << meta.name << " " << meta.version << " (" << exe.string() << ")\n";
      }
      const std::string pname = spec.name;
      if (!meta.hooks.empty()) {
        r->proc.spawn(exe, {},
                      {{"SSG_PROTOCOL", std::to_string(kPluginProtocol)}, {"SSG_BUILD_DATE", date}},
                      [this, pname](const std::string& l) { warn_("plugin " + pname + ": " + l); });
      }
    } catch (const std::exception& e) {
      fail(*r, "", e.what());
      continue;
    }
    run_.push_back(std::move(r));
    Running& rr = *run_.back();
    infos_.push_back(rr.info);
    if (rr.has("init")) {
      Json params = Json::make_object();
      params.set("config", rr.config).set("site", site);
      bool ok;
      call(rr, "init", params, ok);
    }
  }
  // A required=false plugin that died during init is out of the list.
  for (size_t i = run_.size(); i-- > 0;) {
    if (run_[i]->dead) {
      run_.erase(run_.begin() + static_cast<long>(i));
      infos_.erase(infos_.begin() + static_cast<long>(i));
    }
  }
}

std::string PluginHost::transform(const std::string& hook, Json params, const std::string& field,
                                  std::string value) {
  for (auto& r : run_) {
    if (!r->has(hook)) continue;
    params.set(field, Json(value));
    bool ok;
    Json res = call(*r, hook, params, ok);
    if (!ok) continue;
    if (!res.is_string()) {
      fail(*r, hook, "the result must be a string");
      continue;
    }
    value = std::move(res.str);
  }
  return value;
}

bool PluginHost::container(const std::string& name, const Json& params, std::string& markdown) {
  const std::string hook = "container:" + name;
  for (auto& r : run_) {
    if (!r->has(hook)) continue;
    bool ok;
    Json res = call(*r, hook, params, ok);
    if (!ok) return false;
    if (!res.is_string()) {
      fail(*r, hook, "the result must be a string");
      return false;
    }
    markdown = std::move(res.str);
    return true;
  }
  return false;
}

std::vector<PluginFile> PluginHost::assets(const std::string& rel, const fs::path& source,
                                           const fs::path& out_dir) {
  std::vector<PluginFile> out;
  for (auto& r : run_) {
    if (!r->has("asset")) continue;
    Json params = Json::make_object();
    params.set("path", Json(rel)).set("source", Json(source.string())).set("out_dir", Json(out_dir.string()));
    bool ok;
    Json res = call(*r, "asset", params, ok);
    if (!ok) continue;
    try {
      if (!res.is_array()) throw std::runtime_error("the result must be a list of files");
      std::vector<PluginFile> mine;
      for (const Json& f : res.array) {
        const std::string pub = f.str_at("path"), file = f.str_at("file");
        const fs::path norm = fs::path(pub).lexically_normal();
        if (pub.empty() || norm.is_absolute() || norm.generic_string() != pub || pub == "." ||
            starts_with(pub, "../") || pub.find("/../") != std::string::npos) {
          throw std::runtime_error("'" + pub + "' is not a path inside dist");
        }
        fs::path src = file;
        if (src.is_relative()) src = out_dir / src;
        if (file.empty() || !inside(out_dir, src) || !fs::is_regular_file(src)) {
          throw std::runtime_error("'" + file + "' is not a file under out_dir");
        }
        mine.push_back({pub, src});
      }
      out.insert(out.end(), mine.begin(), mine.end());
    } catch (const std::exception& e) {
      fail(*r, "asset", e.what());
    }
  }
  return out;
}

void PluginHost::post_build(const fs::path& manifest, const fs::path& dist) {
  for (auto& r : run_) {
    if (!r->has("post_build")) continue;
    Json params = Json::make_object();
    params.set("manifest", Json(manifest.string())).set("dist", Json(dist.string()));
    bool ok;
    call(*r, "post_build", params, ok);
  }
}

void PluginHost::stop() {
  for (auto& r : run_) {
    if (r->dead || r->info.hooks.empty()) continue;
    r->proc.close_stdin();
    int st = 0;
    if (!r->proc.wait_exit(kStopGraceMs, st)) {
      fail(*r, "", "did not exit when ssg closed its input");
    } else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
      fail(*r, "", describe_status(st));
    }
    r->dead = true;
  }
}
