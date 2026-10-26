# Command line

`ssg` builds a content directory, or renders one markdown file. This page mirrors
[man/ssg.1](../man/ssg.1) (`man ssg`). The site format is in [configuration.md](configuration.md),
[templates.md](templates.md) and [markdown.md](markdown.md), or [man/ssg.5](../man/ssg.5) in one page.

```
ssg [--drafts] [--check-links] [--quiet] <content-dir>
ssg render [--strict] [--text] [--no-html] [--scripts|--no-scripts] [--no-extensions] <file|->
ssg --help | --version
```

- [Building](#building)
- [Build options](#build-options)
- [Link checking](#link-checking)
- [ssg render](#ssg-render)
- [--help, --version](#--help---version)
- [Exit status](#exit-status)
- [Environment](#environment)
- [Files](#files)
- [Examples](#examples)
- [Messages](#messages)

## Building

`ssg <content-dir>` builds the site in `<content-dir>`, which must contain `site.toml`. Each
markdown page becomes three files in dist: `.html` (the page in the HTML skeleton), `.md`
(templates resolved, frontmatter removed) and `.txt` (wrapped plain text). Every other file is
copied as is.

A build runs in this order:

1. Read `site.toml`. Relative paths in it are relative to `<content-dir>`.
2. Refuse an unsafe dist (see [Dist](#dist-exit-1-nothing-written)). Nothing is written before
   this check.
3. Load and check the skeleton.
4. Walk `<content-dir>` top-down, in byte order of file names. Files ending in `.md`
   (case-sensitive) are pages; other regular files are assets. Skipped:
   - names starting with `.`, except directories named `.well-known`;
   - the dist and templates directories, the skeleton, the top-level `site.toml`;
   - paths matched by `.assetignore` files or the `ignore` key;
   - directories that contain a `.git` entry, unless `nested_repos = true`;
   - the contents of symlinks to directories.
5. Empty dist if `clean` is true and ssg created it (it holds a `.ssg-dist` marker).
6. Copy assets.
7. Render pages: a first pass over all pages resolves frontmatter and templates, then the real
   render fills in the navigation keys (`children`, `parent_*`, `prev_*`, `next_*`).
8. Write `sitemap.xml` if `base_url` is set.
9. With `--check-links`, check local links.
10. Print a summary line to stderr.

An error stops the build at once. Assets and pages written before it stay in dist.

## Build options

Options and `<content-dir>` may come in any order. There is no `--` terminator: pass a directory
whose name starts with `-` as `./-dir`, and one named `render` as `./render`.

| option | |
| --- | --- |
| `--drafts` | Also build pages whose `draft` key is true. Without it, a page whose own frontmatter sets a true `draft` isn't rendered at all (a broken template in a draft doesn't fail the build), and a page made a draft by a template is dropped after the first pass. Drafts that aren't built don't count when deciding `foo.md` vs `foo/index.md`; with `--drafts` they do, so a draft `foo/index.md` moves `foo.md` from `/foo/` to `/foo`. |
| `--check-links` | After the build, warn about each `href`/`src` in a page's HTML output (skeleton included) that points at nothing in dist. See [Link checking](#link-checking). Doesn't change the exit status. |
| `--quiet`, `-q` | No summary line and no notes. Warnings and errors still print. |

## Link checking

For each page, `--check-links` reads `href` and `src` values (quoted, unquoted, or with blanks
around `=`) in real tags only. Escaped text (`&lt;a href=...`), HTML comments and the inside of
`<script>`/`<style>` aren't scanned. Copied `.html` assets aren't scanned either.

A value is skipped if it is empty, starts with `#` or `//`, or has a scheme (a `:` before any
`/`, `?` or `#`: `https:`, `mailto:`, ...). Otherwise the query and fragment are cut, basic
entities and `%XX` escapes are decoded, and the path is resolved against the page URL. It exists
if dist has:

| link | file in dist |
| --- | --- |
| `/a/` | `a/index.html` |
| `/a` | `a`, `a/index.html` or `a.html` |

A path that climbs above the site root (`../../x` from `/a/`) is broken, and so is `.ssg-dist`.
Each broken link is reported once per page.

## ssg render

`ssg render <file>` converts one markdown file to HTML on stdout; `-` reads stdin. Frontmatter
isn't parsed, templates aren't evaluated and `site.toml` isn't read: a `---` block renders as
markdown (typically a thematic break and a setext heading). The options start from the built-in defaults: raw
HTML on and sanitized, extensions, heading ids and bare autolinks on, text wrapped at 80 columns.

Options apply left to right, so a later one overrides an earlier one: `--strict --no-html`
escapes raw HTML, `--no-html --strict` doesn't.

| option | |
| --- | --- |
| `--strict` | Plain CommonMark 0.31.2: raw HTML and scripts pass through unchanged, no extensions, no heading ids, no bare autolinks. Same as `--scripts --no-extensions` plus heading ids and autolinks off. |
| `--text` | Plain text instead of HTML, as in the `.txt` output, wrapped at 80 columns. |
| `--no-html` | Escape raw HTML so it shows as text. |
| `--scripts` | Let `<script>` and similar elements, `on*` attributes, and `javascript:`, `vbscript:`, `file:` and non-image `data:` URLs through. |
| `--no-scripts` | Escape those elements, drop those attributes and neutralize those URLs. The default, so it only undoes an earlier `--scripts` or `--strict`. |
| `--no-extensions` | CommonMark syntax only: no tables, strikethrough, task lists, footnotes, definition lists, containers, alerts, `==mark==`, heading attributes or bare autolinks. Heading ids stay on; raw HTML is still sanitized. |

`render` has no way to set the text width or to turn off heading ids alone; a site sets those in
the `[markdown]` table of `site.toml` ([configuration.md](configuration.md)).

```
$ printf '# Hi {#top}\n\n<b onclick="x()">raw</b> www.example.com\n' > t.md
$ ssg render t.md
<h1 id="top">Hi</h1>
<p><b>raw</b> <a href="http://www.example.com">www.example.com</a></p>
$ ssg render --strict t.md
<h1>Hi {#top}</h1>
<p><b onclick="x()">raw</b> www.example.com</p>
$ ssg render --no-html t.md
<h1 id="top">Hi</h1>
<p>&lt;b onclick=&quot;x()&quot;&gt;raw&lt;/b&gt; <a href="http://www.example.com">www.example.com</a></p>
$ ssg render --text t.md
Hi
==

raw <www.example.com>
```

## --help, --version

Recognised only as the first argument; anywhere else they are unknown options (exit 2).

| option | |
| --- | --- |
| `--help`, `-h` | Usage and an option summary on stdout. |
| `--version` | `ssg 0.2.0` on stdout. Later arguments are ignored. |

## Exit status

| status | |
| --- | --- |
| 0 | Success, including builds that printed warnings. |
| 1 | An error: configuration, template, unsafe dist, or file I/O. |
| 2 | Bad usage; the usage text goes to stderr. |

## Environment

`SOURCE_DATE_EPOCH`: seconds since 1970-01-01 UTC, used for `build_date` instead of the current
time. With it set, the same input builds byte-identical output (the summary line's timing
aside). It must be a decimal integer from 0 to 253402300799 (9999-12-31T23:59:59Z); anything
else is ignored with a warning and the current time is used.

```
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) ssg .
```

No other environment variables are read.

## Files

Paths other than `site.toml` are set in `site.toml`; these are the usual names.

| path | |
| --- | --- |
| `<content-dir>/site.toml` | Configuration. Required. |
| `<content-dir>/skeleton.html` | The HTML page every page goes in. |
| `<content-dir>/templates/` | For `{{extend}}` and `{{include}}`. May be missing until a page uses one. |
| `.assetignore` | In any directory: gitignore patterns for files not to publish. |
| `dist/` | Output. |
| `dist/.ssg-dist` | Marker, written when ssg owns dist (dist was missing, empty or already marked). Only a marked dist is emptied. It is deployed with the site unless the deploy excludes it. |
| `dist/sitemap.xml` | Written when `base_url` is set, unless the content dir has its own. |

## Examples

Build and preview:

```
$ ssg mysite
ssg: built 12 pages, copied 4 files in 9 ms
$ python3 -m http.server -d mysite/dist
```

CI, with the builder checked out into `./builder` inside the content dir:

```
$ SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) builder/build/ssg .
ssg: note: skipping builder/ (a nested git checkout; set nested_repos = true to build it)
ssg: built 12 pages, copied 4 files in 9 ms
```

The note is harmless; `--quiet` hides it. List `builder/` in `.assetignore` to skip it silently.

Drafts and broken links:

```
$ ssg --drafts --check-links mysite
ssg: warning: guide/a.md: broken link ../setup/
ssg: built 13 pages, copied 4 files in 11 ms (1 warning)
```

Render a file, or stdin:

```
$ ssg render README.md > readme.html
$ printf '# Hi\n\nSome *text*.\n' | ssg render --text -
Hi
==

Some text.
```

## Messages

Everything except `render` output, `--help` and `--version` goes to stderr. Errors are
`ssg: <message>`, with the source file at fault (relative to the content dir) in front where
there is one:

```
ssg: guide/a.md: missing template "post" (looked for /srv/site/templates/post.md)
```

Warnings (`ssg: warning: ...`) and notes (`ssg: note: ...`) don't change the exit status. The
summary line counts the warnings printed during the build; `site.toml` warnings come before it
and aren't counted. Below, `<src>` is the full path of the file being evaluated.

### Usage (exit 2)

Each is followed by the usage text. `ssg` with no arguments prints only the usage.

| message | |
| --- | --- |
| `unknown option <opt>` | Includes `--` and options of the other mode (`ssg render --drafts`). |
| `one content dir only` | |
| `render takes one input` | |
| `render needs a file or -` | |

### site.toml (exit 1)

| message | |
| --- | --- |
| `missing site.toml in <dir>` | Also what a mistyped or non-directory `<dir>` gives. |
| `site.toml must set dist, templates, and skeleton` | |
| `site.toml:<N>: <message>` | Syntax or type error on line N: `'clean' must be a boolean`, `duplicate key 'dist'`, `duplicate table [markdown]` (names the line after the repeated header), `expected ] after table name`, `dotted keys are not supported`, `only integers are supported`, `inline tables are not supported`, `multi-line strings are not supported`, `arrays may only hold strings`, `unterminated string`, `unknown escape \q`, `expected end of line`, `'text_width' must be between 0 and 100000`, ... See [configuration.md](configuration.md). |
| `warning: site.toml:<N>: unknown key '<k>'` | Also `unknown key '<k>' in [markdown]` and `unknown table [<t>]`. Keys inside an unknown table are ignored without further warnings. |

### Dist (exit 1, nothing written)

| message | |
| --- | --- |
| `dist (<path>) must not be the content dir or contain it` | `dist = "."`, `dist = ".."` |
| `dist (<path>) must not be or contain the templates dir` | |
| `dist (<path>) must not contain the skeleton` | |
| `dist (<path>) must not be inside a hidden dir (<part>)` | Only the parts of dist's path below what it shares with the content dir are checked: `.out/site` and `../.cache/out` are refused, but a content dir that itself sits under a hidden dir (`~/.sites/blog`, with `dist = "dist"`) is fine. |

### Skeleton (exit 1)

Prefixed with the skeleton's path relative to the content dir (`skeleton.html: ...`).

| message | |
| --- | --- |
| `missing HTML skeleton: <path>` | Not prefixed. |
| `HTML skeleton has no <main tag` | Also `has a malformed <main tag`, `has no </main> tag`. Not checked when the skeleton contains `{{content}}`. |
| `missing {{end}} for {{if <key>}}` | Also `{{end}} without {{if}}`, `{{else}} without {{if}}`, `second {{else}} in one {{if}}`. |

### Pages and templates (exit 1)

Prefixed with the page's path relative to the content dir.

| message | |
| --- | --- |
| `frontmatter line <N>: expected key: value, got: <line>` | Line 1 is the opening `---`. In a template's frontmatter, the template's path comes first. |
| `unclosed frontmatter (missing closing ---)` | |
| `missing template "<name>" (looked for <path>)` | For an `{{include}}`, ` in <src>` follows. |
| `bad template path "<name>" in <src> (must be relative to the templates dir, without "..")` | Absolute, with a `..` part, or an empty `{{include ""}}`. An empty `{{extend ""}}` is dropped silently (no template). |
| `cyclic {{extend}} in <src>` | |
| `cyclic {{include "<name>"}} in <src> (in include <path>)` | |
| `{{extend}} inside included template <path> in <src>` | Partials can't extend. |
| `missing {{end}} for {{if <key>}} in <src>` | Also `{{end}} without {{if}}`, `{{else}} without {{if}}`, `second {{else}} in one {{if}}`, each followed by ` in <src>`. |
| `{{if}} nested too deeply in <src>` | More than 256 levels. |
| `template expansion nested more than 64 levels deep in <src>` | Keys and includes. |
| `template expansion larger than 64 MiB in <src>` | |

### Files (exit 1)

| message | |
| --- | --- |
| `cannot read file: <path>` | Missing or unreadable (`ssg render nofile.md`). |
| `cannot read <path>: is a directory` | |
| `error reading file: <path>` | |
| `cannot write file: <path>` | |
| `error writing file (disk full?): <path>` | |
| `error writing output` | `render` couldn't write stdout. |

Other filesystem failures (permissions, for example) come with the C++ library's message.

### Warnings

| message | |
| --- | --- |
| `not cleaning <path>: it wasn't created by ssg (no .ssg-dist); delete it once by hand` | dist isn't empty and has no marker, e.g. output of an older ssg. It's built over, never emptied, until deleted by hand. Also `not cleaning <path>: not a plain directory` when dist is a symlink or a file. Neither appears with `clean = false`. |
| `<a> and <b> both write <out> (the latter wins)` | Two sources produce the same file, e.g. an asset `index.html` and the page `index.md`. Pages are written after assets, so the page wins. |
| `sitemap.xml exists in the content dir; not generating one` | Only with `base_url` set; the content dir's file is copied. |
| `ignoring SOURCE_DATE_EPOCH=<value> (want seconds from 0 to 253402300799); using the current time` | |
| `<page>: broken link <link>` | From `--check-links`. `<link>` is the value from the page, decoded, without query or fragment. |

### Notes and summary

| message | |
| --- | --- |
| `note: skipping <dir>/ (a nested git checkout; set nested_repos = true to build it)` | `<dir>` has a `.git` file or directory. Hidden by `--quiet`; not a warning. |
| `built <N> pages, copied <M> files in <T> ms (<K> warnings)` | The summary. The part in parentheses only appears when there were warnings. Hidden by `--quiet`. |
