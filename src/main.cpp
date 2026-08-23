#include "build.hpp"
#include "config.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ssg <content-dir>\n";
    return 2;
  }
  try {
    const auto content = std::filesystem::path(argv[1]);
    build_site(load_site_config(content));
  } catch (const std::exception& e) {
    std::cerr << "ssg: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
