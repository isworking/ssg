# Fuzz targets

One libFuzzer target per parser that reads untrusted input. Each feeds the fuzzer's bytes to the
code under test and catches `std::runtime_error`, which is how ssg reports a user error (bad
`site.toml`, unknown template, ...). Anything else, such as a crash, a sanitizer report, a hang or
another exception type, is a bug.

| Target | Code under test | Input |
| --- | --- | --- |
| `fuzz_markdown_html` | `markdown_to_html`, `markdown_headings` | Markdown, with default options and in strict CommonMark mode |
| `fuzz_markdown_text` | `markdown_to_text` | Markdown, at widths 80 and 12 |
| `fuzz_sanitizer` | `markdown_to_html` with `raw_html = true`, `scripts = false` | Markdown, preferably HTML-heavy |
| `fuzz_template` | `parse_markdown_source`, `render_extended`, `interpolate` | A page: frontmatter and template syntax. The templates dir doesn't exist, so `{{extend}}` and `{{include}}` stop at "missing template" |
| `fuzz_toml` | `parse_toml` | A `site.toml` |
| `fuzz_json` | `parse_json`, `Json::dump` (the plugin protocol's reader) | A JSON document |
| `fuzz_ignore` | `IgnoreRules`, `glob_match` | Pattern lines, then the path to test on the last line |

Seed corpora are in `corpus/<target>/`. They are copies of `tests/markdown/*.md`, the example
site's templates and `site.toml`, and the `.assetignore` fixtures, plus a few hand-written seeds.

## Build with libFuzzer

This needs LLVM clang. Apple clang ships without libFuzzer, so on macOS install LLVM
(`brew install llvm`) and point CMake at it:

```
cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ \
      -DSSG_FUZZ=ON -DSSG_SANITIZE=ON
cmake --build build-fuzz -j
```

On Linux, `-DCMAKE_CXX_COMPILER=clang++` is enough. `SSG_SANITIZE=ON` adds ASan and UBSan.
Leave it on: without it, most memory bugs don't crash.

## Run

```
mkdir -p /tmp/fuzz-md && cp fuzz/corpus/fuzz_markdown_html/* /tmp/fuzz-md/
build-fuzz/fuzz/fuzz_markdown_html /tmp/fuzz-md -max_total_time=60
```

The first directory is read and also receives new interesting inputs, so pass a scratch copy to
keep `fuzz/corpus` small. A crash is written as `crash-<sha1>` in the current directory. Replay it
with `build-fuzz/fuzz/fuzz_markdown_html crash-<sha1>`, fix the bug, and add a regression test
under `tests/`.

CI runs each target for 60 seconds on every push (the `fuzz` job in
`.github/workflows/ci.yml`).

## Without libFuzzer

`-DSSG_FUZZ_STANDALONE=ON` builds the same targets with `standalone_main.cpp`, a `main()` that
passes each file named on the command line, or each file under a named directory, to the target
once. It doesn't fuzz; use it to replay a corpus or a crash file with any compiler:

```
cmake -B build-replay -DCMAKE_BUILD_TYPE=Debug -DSSG_FUZZ_STANDALONE=ON -DSSG_SANITIZE=ON
cmake --build build-replay -j
for t in markdown_html markdown_text sanitizer template toml ignore json; do
  build-replay/fuzz/fuzz_$t fuzz/corpus/fuzz_$t || echo "fuzz_$t failed"
done
```
