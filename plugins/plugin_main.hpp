#pragma once

// The protocol 1 loop shared by the first-party plugins: `--meta` prints the description, and
// otherwise each stdin line is one request and gets one reply line on stdout.
#include "json.hpp"

#include <functional>
#include <iostream>
#include <string>

struct PluginContext {
  Json config = Json::make_object();  // the plugin's [plugin.<name>] keys with defaults filled in
  Json site = Json::make_object();    // what init was sent: base_url, content_dir, dist_dir, ...
};

// handler(hook, params, ctx) returns the result; throw std::exception to answer with an error.
inline int plugin_main(int argc, char** argv, const std::string& meta,
                       const std::function<Json(const std::string&, const Json&, PluginContext&)>& handler) {
  if (argc > 1 && std::string(argv[1]) == "--meta") {
    std::cout << meta << "\n";
    return 0;
  }
  PluginContext ctx;
  std::string line;
  while (std::getline(std::cin, line)) {
    Json reply = Json::make_object();
    try {
      const Json req = parse_json(line);
      if (const Json* id = req.find("id")) reply.set("id", *id);
      const std::string hook = req.str_at("hook");
      const Json* params = req.find("params");
      const Json none = Json::make_object();
      if (hook == "init" && params) {
        if (const Json* c = params->find("config")) ctx.config = *c;
        if (const Json* s = params->find("site")) ctx.site = *s;
      }
      reply.set("result", handler(hook, params ? *params : none, ctx));
    } catch (const std::exception& e) {
      reply.object.erase("result");
      reply.set("error", Json(e.what()));
    }
    std::cout << reply.dump() << std::endl;
  }
  return 0;
}
