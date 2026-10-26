# ssg

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
- Raw HTML on by default, sanitized: `<script>`-type elements are escaped (shown as text), `on*=`
  attributes and `javascript:` URLs are dropped unless `scripts = true`.
- Templates: `{{key}}`, `{{extend "base"}}`, `{{include "partial"}}`, `{{if key}}` /
  `{{if not key}}` / `{{else}}` / `{{end}}`, `\{{` escapes. Layouts nest.
- Built-in keys: `url`, `path`, `build_date`, `word_count`, `reading_time`, `toc`, `children`,
  `parent_*`, `prev_*`, `next_*`. Series and sections need no config: sort by `order`, then
  `date`.
- One HTML skeleton per site: `<title>` and `<main>` are filled in, `{{key}}` is HTML-escaped.
- Drafts (`draft: "true"`, built with `--drafts`), `sitemap.xml` (when `base_url` is set),
  `--check-links`.
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
| [docs/cli.md](docs/cli.md) | Commands, flags, exit codes, messages |
| [man/ssg.1](man/ssg.1), [man/ssg.5](man/ssg.5) | Manual pages: the command, and the site format (`man ssg`, `man 5 ssg` once installed) |

[docs/README.md](docs/README.md) indexes the docs. [`example/`](example/) is a complete site
that uses most features.

## CLI

```
ssg [--drafts] [--check-links] [--quiet] <content-dir>
ssg render [--strict] [--text] [--no-html] [--scripts|--no-scripts] [--no-extensions] <file|->
ssg --help | --version
```

Errors print `ssg: <message>`, prefixed with the file at fault where there is one
(`ssg: guide/a.md: missing template "post" (looked for ...)`), and exit 1. Bad usage prints the
usage and exits 2. Warnings (`ssg: warning: ...`) and notes (`ssg: note: ...`) don't change the
exit status. See [docs/cli.md](docs/cli.md).

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

CommonMark conformance needs the spec, which isn't vendored:

```
curl -o tests/spec.json https://spec.commonmark.org/0.31.2/spec.json
python3 tests/commonmark.py build/ssg tests/spec.json   # --section, --example N, --verbose
```

If `tests/spec.json` and Python 3 are present when CMake configures, this is also the
`commonmark` ctest, which requires 100%.
