#include "serve.hpp"
#include "util.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

uint64_t tree_signature(const fs::path& content, const fs::path& skip) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  auto mix = [&](uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
  };
  std::error_code ec;
  const auto skip_norm = skip.lexically_normal();
  fs::recursive_directory_iterator it(content, fs::directory_options::skip_permission_denied, ec);
  for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
    const auto& p = it->path();
    const std::string name = p.filename().string();
    if (p.lexically_normal() == skip_norm || (!name.empty() && name[0] == '.')) {
      if (it->is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    for (const char ch : p.string()) mix(static_cast<unsigned char>(ch));
    if (it->is_regular_file(ec)) {
      mix(static_cast<uint64_t>(it->file_size(ec)));
      mix(static_cast<uint64_t>(it->last_write_time(ec).time_since_epoch().count()));
    }
  }
  return h;
}

namespace {

void rebuild_quietly(const std::function<void()>& rebuild) {
  try {
    rebuild();
  } catch (const std::exception& e) {
    std::cerr << "ssg: " << e.what() << "\n";
  }
}

const char* kReloadScript =
    "<script>(function(){var v=null;function p(){fetch('/__ssg/version',{cache:'no-store'})"
    ".then(function(r){return r.text()}).then(function(t){if(v===null)v=t;"
    "else if(t!==v)location.reload()}).catch(function(){})}setInterval(p,700);p()})();"
    "</script>";

std::string content_type(const fs::path& p) {
  static const std::map<std::string, std::string> types = {
      {".html", "text/html; charset=utf-8"},
      {".css", "text/css; charset=utf-8"},
      {".js", "text/javascript; charset=utf-8"},
      {".mjs", "text/javascript; charset=utf-8"},
      {".json", "application/json"},
      {".xml", "application/xml"},
      {".txt", "text/plain; charset=utf-8"},
      {".md", "text/markdown; charset=utf-8"},
      {".svg", "image/svg+xml"},
      {".png", "image/png"},
      {".jpg", "image/jpeg"},
      {".jpeg", "image/jpeg"},
      {".gif", "image/gif"},
      {".webp", "image/webp"},
      {".avif", "image/avif"},
      {".ico", "image/x-icon"},
      {".woff2", "font/woff2"},
      {".woff", "font/woff"},
      {".pdf", "application/pdf"},
      {".wasm", "application/wasm"},
  };
  const auto it = types.find(p.extension().string());
  return it == types.end() ? "application/octet-stream" : it->second;
}

std::string percent_decode_path(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

void send_all(int fd, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
    if (n <= 0) return;
    off += static_cast<size_t>(n);
  }
}

void respond(int fd, int status, const char* reason, const std::string& type,
             const std::string& body, bool head) {
  std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: " +
                    type + "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  if (!head) out += body;
  send_all(fd, out);
}

// The file a URL path names inside dist, or empty. ".." never resolves.
fs::path locate(const fs::path& dist, const std::string& url_path) {
  fs::path rel;
  size_t i = 0;
  while (i <= url_path.size()) {
    size_t j = url_path.find('/', i);
    if (j == std::string::npos) j = url_path.size();
    const std::string part = url_path.substr(i, j - i);
    if (part == "..") return {};
    if (!part.empty() && part != ".") rel /= part;
    i = j + 1;
  }
  std::error_code ec;
  const fs::path base = fs::weakly_canonical(dist, ec);
  std::vector<fs::path> cands = {dist / rel / "index.html"};
  if (!rel.empty()) {
    cands.insert(cands.begin(), dist / rel);
    cands.push_back(fs::path((dist / rel).string() + ".html"));
  }
  for (const fs::path& cand : cands) {
    if (!fs::is_regular_file(cand, ec)) continue;
    // A symlink must not lead out of dist.
    const auto real = fs::weakly_canonical(cand, ec);
    if (real.string().compare(0, base.string().size(), base.string()) != 0) continue;
    return cand;
  }
  return {};
}

void handle(int fd, const fs::path& dist, unsigned long version) {
  timeval tv{2, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  std::string req;
  char buf[4096];
  while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0) break;
    req.append(buf, static_cast<size_t>(n));
  }
  const size_t eol = req.find("\r\n");
  if (eol == std::string::npos) return;
  const std::string line = req.substr(0, eol);
  const size_t sp1 = line.find(' '), sp2 = line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    respond(fd, 400, "Bad Request", "text/plain", "bad request\n", false);
    return;
  }
  const std::string method = line.substr(0, sp1);
  const bool head = method == "HEAD";
  if (method != "GET" && !head) {
    respond(fd, 405, "Method Not Allowed", "text/plain", "method not allowed\n", false);
    return;
  }
  std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  target = target.substr(0, target.find_first_of("?#"));
  const std::string path = percent_decode_path(target);
  if (path.find('\0') != std::string::npos || path.empty() || path[0] != '/') {
    respond(fd, 400, "Bad Request", "text/plain", "bad request\n", false);
    return;
  }
  if (path == "/__ssg/version") {
    respond(fd, 200, "OK", "text/plain", std::to_string(version), head);
    return;
  }
  int status = 200;
  fs::path file = locate(dist, path);
  if (file.empty()) {
    status = 404;
    // 404.md publishes as 404/index.html; a hand-made 404.html works too.
    file = locate(dist, "/404/");
    if (file.empty()) file = locate(dist, "/404.html");
    if (file.empty()) {
      respond(fd, 404, "Not Found", "text/plain; charset=utf-8", "404 not found\n", head);
      return;
    }
  }
  std::string body;
  try {
    body = read_file(file);
  } catch (const std::exception&) {
    respond(fd, 500, "Internal Server Error", "text/plain", "cannot read file\n", head);
    return;
  }
  const std::string type = content_type(file);
  if (starts_with(type, "text/html")) {
    const size_t at = body.rfind("</body>");
    body.insert(at == std::string::npos ? body.size() : at, kReloadScript);
  }
  respond(fd, status, status == 200 ? "OK" : "Not Found", type, body, head);
}

}  // namespace

int watch_site(const fs::path& content, const fs::path& dist, const std::function<void()>& rebuild) {
  rebuild_quietly(rebuild);
  std::cerr << "ssg: watching " << content.string() << " (Ctrl-C to stop)\n";
  uint64_t seen = tree_signature(content, dist);
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const uint64_t now = tree_signature(content, dist);
    if (now == seen) continue;
    seen = now;
    rebuild_quietly(rebuild);
  }
}

int serve_site(const fs::path& content, const fs::path& dist, const ServeOptions& opt,
               const std::function<void()>& rebuild) {
  ::signal(SIGPIPE, SIG_IGN);
  rebuild_quietly(rebuild);

  const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (lfd < 0) throw std::runtime_error("cannot create a socket");
  const int on = 1;
  ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(opt.port));
  if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(lfd, 16) != 0) {
    ::close(lfd);
    throw std::runtime_error("cannot listen on 127.0.0.1:" + std::to_string(opt.port));
  }
  socklen_t len = sizeof addr;
  ::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &len);
  const std::string url = "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port)) + "/";
  std::cerr << "ssg: serving " << dist.string() << " at " << url << " (Ctrl-C to stop)\n";
  if (opt.open) {
#ifdef __APPLE__
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    if (std::system((std::string(opener) + " '" + url + "' >/dev/null 2>&1").c_str()) != 0) {
      std::cerr << "ssg: " << warning_label() << " could not open a browser\n";
    }
  }

  unsigned long version = 1;
  uint64_t seen = tree_signature(content, dist);
  auto last_check = std::chrono::steady_clock::now();
  for (;;) {
    pollfd p{lfd, POLLIN, 0};
    if (::poll(&p, 1, 250) > 0 && (p.revents & POLLIN)) {
      const int cfd = ::accept(lfd, nullptr, nullptr);
      if (cfd >= 0) {
        handle(cfd, dist, version);
        ::close(cfd);
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_check < std::chrono::milliseconds(250)) continue;
    last_check = now;
    const uint64_t sig = tree_signature(content, dist);
    if (sig == seen) continue;
    seen = sig;
    rebuild_quietly(rebuild);
    ++version;
  }
}
