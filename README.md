# ssg

[![CI](https://github.com/isworking/ssg/actions/workflows/ci.yml/badge.svg)](https://github.com/isworking/ssg/actions/workflows/ci.yml)

CommonMark 0.31.2: 652/652 spec examples.

Static site builder. Markdown in; `.html`, `.md` and `.txt` out for every page.

One C++17 binary, no dependencies. Made for sites served with content negotiation (browsers get
HTML, `curl` gets text, `Accept: text/markdown` gets markdown), but the output is plain files that
any static host serves.

```
cmake -B build && cmake --build build
./build/ssg path/to/content
```

## Features

- Three outputs per page: `foo.md` becomes `foo/index.html`, `foo/index.md` (templates resolved,
  frontmatter gone) and `foo/index.txt` (wrapped plain text).
- Markdown: CommonMark 0.31.2 (all 652 spec examples), plus GFM tables, task lists,
  strikethrough, bare autolinks and footnotes, and definition lists, `> [!NOTE]` alerts, `:::`
  containers, `==mark==`, heading ids and `{#id .class}` attributes. `extensions = false` gives
  strict CommonMark.
- Raw HTML on by default, through an allow-list: only known-harmless tags and attributes pass;
  `<script>`, `<iframe>`, `<svg>`, custom elements and the like are escaped (shown as text) and
  `on*=` attributes and `javascript:` URLs are dropped unless `scripts = true`. `allow_tags` and
  `allow_attrs` extend the list. Symlinks to files are skipped unless `follow_symlinks = true`.
- Templates: `{{key}}`, `{{extend "base"}}`, `{{include "partial"}}`, `{{if key}}` /
  `{{if not key}}` / `{{else}}` / `{{end}}`, `\{{` escapes. Layouts nest.
- Built-in keys: `url`, `path`, `build_date`, `word_count`, `reading_time`, `toc`, `children`,
  `parent_*`, `prev_*`, `next_*`, `canonical_url`, `og_title`, and `data.<file>.<key>` from
  `data/*.toml` and `data/*.json`. Series and sections need no config: sort by `order`, then
  `date`.
- One HTML skeleton per site: `<title>` and `<main>` are filled in, `{{key}}` is HTML-escaped.
- Drafts (`draft: "true"`, built with `--drafts`), scheduled pages (a `date` after the build date,
  unless `future = true`), `sitemap.xml` (when `base_url` is set), `--check-links`.
- Generated files: `feed.xml` (Atom, from `[feed]`), `_redirects` (from `redirects.txt`, with
  optional meta-refresh pages), a default `robots.txt`; a hand-written file of the same name wins.
- CSS, opt-in: `{{asset "style.css"}}` links a content-hashed copy (`style.<hash>.css`),
  `{{inline "style.css"}}` embeds it in a `<style>` (its CSP hash goes in the manifest), and
  `[css] minify = true` runs a conservative minifier.
- Lists without loops: `[taxonomy.tags]` makes `/tags/` and `/tags/<term>/` pages, and
  `paginate: "10"` splits an index page's `{{children}}` into `/dir/page/2/`, ...; both hand
  their lists to templates as Markdown-valued keys. See `example/blog/`.
- `manifest.json` beside dist: every page, its outputs and content types, assets (schema 1, see
  [docs](docs/configuration.md#manifestjson)).
- Plugins (protocol 1): `[plugin.<name>]` runs an external `ssg-plugin-<name>` that
  speaks JSON lines, with hooks for the Markdown, `:::` containers, finished pages, assets and the
  finished build. Three come in the box: `highlight` (code blocks), `images` (resized copies;
  needs ImageMagick or `sips`) and `search` (a JSON index). See [docs/plugins.md](docs/plugins.md).
- Publishing rules: everything that isn't markdown is copied as is. Hidden files (`.git`,
  `.env`, ...) never ship except `.well-known/`. Nested git checkouts are skipped unless
  `nested_repos = true`. `.assetignore` files and `ignore = [...]` follow gitignore rules exactly.
- Safe output dir: dist is emptied before each build only if ssg created it (it holds a
  `.ssg-dist` marker). A dist that is, or contains, the sources is refused.
- Reproducible: with `SOURCE_DATE_EPOCH` set (it sets `build_date`), the same input gives
  byte-identical output.

## Build and install

Needs CMake 3.16+ and a C++17 compiler.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build                    # /usr/local: bin/ssg, share/man/man1/ssg.1, man5/ssg.5
cmake --install build --prefix ~/.local  # or anywhere else
```

`compile_commands.json` is written to `build/` for editors and clangd.

`ssg --version` prints the version and the commit CMake saw when it configured, e.g.
`ssg 1.0.0-dev (abc1234)`. Release builds set the suffix: `-DSSG_VERSION_SUFFIX=-alpha.1`, or
`""` for a final release.

Build options (all off by default except `SSG_PLUGINS`):

| option | effect |
| --- | --- |
| `-DSSG_PLUGINS=OFF` | Skip building and installing the first-party plugins (on by default). |
| `-DSSG_WERROR=ON` | Warnings are errors (`-Werror`, `/WX`). CI builds with it. |
| `-DSSG_SANITIZE=ON` | AddressSanitizer and UndefinedBehaviorSanitizer (GCC or Clang). |
| `-DSSG_FUZZ=ON` | libFuzzer targets in `fuzz/`; needs LLVM clang. See [fuzz/README.md](fuzz/README.md). |
| `-DSSG_FUZZ_STANDALONE=ON` | The same targets as plain programs that replay input files, with any compiler. |

## Quick start

```
mkdir -p mysite && cd mysite

cat > site.toml <<'EOF'
dist = "dist"
templates = "templates"
skeleton = "skeleton.html"
EOF

cat > skeleton.html <<'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title></title>
</head>
<body>
<main>
</main>
</body>
</html>
EOF

cat > index.md <<'EOF'
---
title: "Hello"
---
# Hello

First page.
EOF

ssg .
```

```
ssg: built 1 page, copied 0 files in 5 ms
```

```
dist/.ssg-dist
dist/index.html
dist/index.md
dist/index.txt
```

`dist/index.html` is the skeleton with `<title>Hello</title>` and the page in `<main>`. The
templates dir may be missing until a page uses `{{extend}}` or `{{include}}`.

Preview with any static server, e.g. `python3 -m http.server -d dist`. Next:
[docs/getting-started.md](docs/getting-started.md) adds templates, a series with navigation, a
table of contents, and deployment.

## Documentation

| | |
| --- | --- |
| [docs/getting-started.md](docs/getting-started.md) | Tutorial: first site to deployed site, Cloudflare Workers and nginx configs |
| [docs/configuration.md](docs/configuration.md) | `site.toml`, publishing rules, `.assetignore`, dist and cleaning |
| [docs/templates.md](docs/templates.md) | Frontmatter, template syntax, built-in keys, the skeleton, navigation |
| [docs/markdown.md](docs/markdown.md) | Markdown dialect, extensions, sanitizing, plain-text output |
| [docs/plugins.md](docs/plugins.md) | The plugin protocol, hooks, failures, and the first-party plugins |
| [docs/deploy.md](docs/deploy.md) | Cloudflare, Vercel and nginx adapters (`--target`) and the headers they send |
| [docs/cli.md](docs/cli.md) | Commands, flags, exit codes, messages |
| [docs/stability.md](docs/stability.md) | What 1.x freezes |
| [docs/migrating-from-0.2.md](docs/migrating-from-0.2.md) | Upgrading a 0.2 site; see also [CHANGELOG.md](CHANGELOG.md) |
| [man/ssg.1](man/ssg.1), [man/ssg.5](man/ssg.5) | Manual pages: the command, and the site format (`man ssg`, `man 5 ssg` once installed) |

[docs/README.md](docs/README.md) indexes the docs. [`example/`](example/) is a complete site
that uses most features.

## CLI

```
ssg [build] [--drafts] [--check-links] [--incremental] [--target <name>] [options] [<content-dir>]
ssg init|serve|watch|check|list|clean|doctor [options] [<dir>]
ssg new <path>
ssg plugins list | plugins meta <name>
ssg render [--strict] [--text] [--no-html] [--scripts|--no-scripts] [--no-extensions] <file|->
ssg completions bash|zsh|fish
ssg --help | --version
```

`ssg <dir>` is `ssg build <dir>`. `--incremental` keeps dist and rewrites only changed files. Global options: `--config`, `--output`, `--base-url`,
`--verbose`, `--quiet`, `--color`, `--json`, `--fail-on-warning`, `--no-plugins`. Exit status: 0
ok, 1 error, 2 usage, 3 warnings with `--fail-on-warning`.

Errors print `ssg: <message>`, prefixed with the file at fault where there is one
(`ssg: guide/a.md: missing template "post" (looked for ...)`), and exit 1. Bad usage prints the
usage and exits 2. Warnings (`ssg: warning: ...`) and notes (`ssg: note: ...`) don't change the
exit status unless you pass `--fail-on-warning` (then 3). See [docs/cli.md](docs/cli.md).

## Tests

```
cmake -B build && cmake --build build && (cd build && ctest --output-on-failure)
sh tests/run.sh build/ssg            # the "sites" ctest on its own
sh tests/run.sh build/ssg --update   # regenerate tests/expected/example after an intended change
```

`tests/run.sh`:

1. builds `example/` (with `SOURCE_DATE_EPOCH=0`) and diffs it against `tests/expected/example/`;
2. runs each `tests/sites/<name>.check`, a sh snippet that builds a scratch copy of
   `tests/sites/<name>/` (or a site it writes itself) and asserts on dist and stderr;
3. renders each `tests/markdown/X.md` and compares it with `X.html` and (`--text`) `X.txt`.
   `--update` never touches these.

The `unit` ctest (`tests/unit/config_test.cpp`) checks what `site.toml` loading stores that the
build output doesn't show, such as the `[plugin.<name>]` tables. `json_test` covers the JSON reader the
plugin protocol uses, and `tests/plugin/mock_plugin.cpp` is the plugin that `tests/sites/plugin.check`
drives through every hook and failure.

CommonMark conformance needs the spec, which isn't vendored:

```
curl -o tests/spec.json https://spec.commonmark.org/0.31.2/spec.json
python3 tests/commonmark.py build/ssg tests/spec.json   # --section, --example N, --verbose
```

If `tests/spec.json` and Python 3 are present when CMake configures, this is also the
`commonmark` ctest, which requires 100%. CI fetches the spec (checking its sha256) and runs every
test on Linux (GCC, Clang) and macOS in Debug and Release, plus a sanitizer build and 60 seconds
of each fuzz target: [.github/workflows/ci.yml](.github/workflows/ci.yml).
