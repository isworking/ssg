// A main() for the fuzz targets without libFuzzer (SSG_FUZZ_STANDALONE): runs each file named on
// the command line, or each file in a named directory, through LLVMFuzzerTestOneInput once.
// Built with SSG_SANITIZE=ON it replays a corpus or a crash file under ASan and UBSan.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace {

void run_file(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

}  // namespace

int main(int argc, char** argv) {
  size_t n = 0;
  for (int i = 1; i < argc; ++i) {
    const std::filesystem::path arg(argv[i]);
    if (std::filesystem::is_directory(arg)) {
      std::vector<std::filesystem::path> files;
      for (const auto& e : std::filesystem::recursive_directory_iterator(arg)) {
        if (e.is_regular_file()) files.push_back(e.path());
      }
      std::sort(files.begin(), files.end());
      for (const auto& f : files) {
        run_file(f);
        ++n;
      }
    } else {
      run_file(arg);
      ++n;
    }
  }
  std::cerr << argv[0] << ": ran " << n << " input(s)\n";
  return 0;
}
