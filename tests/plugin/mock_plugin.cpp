// ssg-plugin-mock: a protocol 1 plugin for the tests. It exercises every hook and, on request,
// fails in the ways a real plugin can. Set in [plugin.mock]:
//   fail_hook = "post_page"   which hook misbehaves ("" = none)
//   fail      = "exit" | "badjson" | "hang" | "error" | "wrongid" | "nonstring" | "escape"
//   warn      = "text"        written to stderr during init
//   log       = "path"        every hook name received is appended there
// and MOCK_META_PROTOCOL=2 in the environment makes --meta claim a newer protocol.
#include "json.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
  return s;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

void spit(const std::string& path, const std::string& data) {
  std::ofstream(path, std::ios::binary) << data;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--meta") {
    const char* mp = std::getenv("MOCK_META_PROTOCOL");
    std::cout << "{\"name\":\"mock\",\"version\":\"0.3.1\",\"protocol\":" << (mp ? mp : "1")
              << ",\"pure\":true,\"description\":\"test plugin\",\"hooks\":[\"init\","
                 "\"transform_source\",\"post_page\",\"asset\",\"post_build\",\"container:shout\"],"
                 "\"config\":{\"fail_hook\":{\"type\":\"string\",\"default\":\"\"},"
                 "\"fail\":{\"type\":\"string\",\"default\":\"\"},"
                 "\"warn\":{\"type\":\"string\",\"default\":\"\"},"
                 "\"log\":{\"type\":\"string\",\"default\":\"\"},"
                 "\"suffix\":{\"type\":\"string\",\"default\":\"-mock\"},"
                 "\"count\":{\"type\":\"int\",\"default\":3}}}\n";
    return 0;
  }
  Json config = Json::make_object();
  std::string line;
  while (std::getline(std::cin, line)) {
    const Json req = parse_json(line);
    const std::string hook = req.str_at("hook");
    const Json& params = *req.find("params");
    Json result;
    if (hook == "init") {
      config = *params.find("config");
      if (!config.str_at("warn").empty()) std::cerr << config.str_at("warn") << "\n";
      result = Json("ok");
      // The environment carries the build date for plugins that don't read init.
      if (config.str_at("fail_hook") == "init_env" && !std::getenv("SSG_BUILD_DATE")) return 9;
    } else if (hook == "transform_source") {
      result = Json(replace_all(params.str_at("markdown"), "TRANSFORM_ME", "transformed by mock"));
    } else if (hook == "container:shout") {
      result = Json("args=[" + params.str_at("args") + "] page=" + params.str_at("page") + "\n\n" +
                    upper(params.str_at("body")));
    } else if (hook == "post_page") {
      result = Json(replace_all(params.str_at("html"), "</body>",
                                "<!-- mock " + params.str_at("url") + " --></body>"));
    } else if (hook == "asset") {
      Json files = Json::make_array();
      const std::string path = params.str_at("path");
      if (path.size() > 4 && path.substr(path.size() - 4) == ".bin") {
        const std::string base = path.substr(path.rfind('/') + 1) + config.str_at("suffix");
        spit(params.str_at("out_dir") + "/" + base, "mock:" + slurp(params.str_at("source")));
        Json f = Json::make_object();
        f.set("path", Json(config.str_at("fail") == "escape" && config.str_at("fail_hook") == "asset"
                               ? "../evil"
                               : path + config.str_at("suffix")));
        f.set("file", Json(base));
        files.push(f);
      }
      result = files;
    } else if (hook == "post_build") {
      const Json manifest = parse_json(slurp(params.str_at("manifest")));
      spit(params.str_at("dist") + "/mock-build.txt",
           "pages=" + std::to_string(manifest.find("pages")->array.size()) + "\n");
    } else {
      Json err = Json::make_object();
      err.set("id", *req.find("id")).set("error", Json("unknown hook " + hook));
      std::cout << err.dump() << std::endl;
      continue;
    }
    const std::string log = config.str_at("log");
    if (!log.empty()) {
      std::ofstream(log, std::ios::app) << hook << " " << std::getenv("SSG_PROTOCOL") << "\n";
    }
    if (config.str_at("fail_hook") == hook) {
      const std::string how = config.str_at("fail");
      if (how == "exit") return 3;
      if (how == "badjson") { std::cout << "this is not json" << std::endl; continue; }
      if (how == "hang") { sleep(60); }
      if (how == "error") {
        Json err = Json::make_object();
        err.set("id", *req.find("id")).set("error", Json("deliberate failure"));
        std::cout << err.dump() << std::endl;
        continue;
      }
      if (how == "wrongid") {
        Json r = Json::make_object();
        r.set("id", Json(999)).set("result", result);
        std::cout << r.dump() << std::endl;
        continue;
      }
      if (how == "nonstring" && hook != "asset" && hook != "init" && hook != "post_build") result = Json(5);
    }
    Json res = Json::make_object();
    res.set("id", *req.find("id")).set("result", result);
    std::cout << res.dump() << std::endl;
  }
  return 0;
}
