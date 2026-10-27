// The JSON reader and writer used by the plugin protocol.
#include "json.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (ok) return;
  std::cerr << "FAIL: " << what << "\n";
  ++failures;
}

bool rejects(const std::string& text) {
  try {
    parse_json(text);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  const Json j = parse_json(R"( {"b": [1, -2, 3.5, true, null, "x\n\u00e9\ud83d\ude00"], "a": {"k": ""}} )");
  check(j.is_object() && j.find("a") && j.find("b"), "an object with two members");
  const Json& b = *j.find("b");
  check(b.array.size() == 6 && b.array[0].kind == Json::Int && b.array[1].integer == -2, "integers");
  check(b.array[2].kind == Json::Real && b.array[2].real == 3.5, "a real");
  check(b.array[3].boolean && b.array[4].kind == Json::Null, "true and null");
  check(b.array[5].str == "x\n\xc3\xa9\xf0\x9f\x98\x80", "escapes, including a surrogate pair, become UTF-8");
  check(j.dump() == "{\"a\":{\"k\":\"\"},\"b\":[1,-2,3.5,true,null,\"x\\n\xc3\xa9\xf0\x9f\x98\x80\"]}",
        "dump is one line with sorted keys: " + j.dump());
  check(parse_json("\"\\ud800\"").str == "\xef\xbf\xbd", "a lone surrogate becomes U+FFFD");
  check(parse_json("123456789012345678901").kind == Json::Real, "a huge integer reads as a real");

  Json o = Json::make_object();
  o.set("s", Json("a\"b\\c\x01")).set("n", Json(7)).set("t", Json(true));
  check(o.dump() == "{\"n\":7,\"s\":\"a\\\"b\\\\c\\u0001\",\"t\":true}", "building and printing: " + o.dump());
  check(o.str_at("s") == "a\"b\\c\x01" && o.str_at("n", "def") == "def" && o.str_at("zz", "d") == "d",
        "str_at falls back for missing and non-string members");

  for (const char* bad : {"", "{", "[1,]", "{\"a\":}", "{a:1}", "01", "1.", "\"\x01\"", "\"\\x\"",
                          "[1] 2", "nul", "\"abc", "{\"a\" 1}", "\"\\u12\"", "-"}) {
    check(rejects(bad), std::string("rejects: ") + bad);
  }
  check(rejects(std::string(70, '[') + std::string(70, ']')), "rejects nesting past 64 levels");
  check(!rejects(std::string(60, '[') + std::string(60, ']')), "accepts 60 levels");

  if (failures) return 1;
  std::cout << "json_test: ok\n";
  return 0;
}
