# Command line

`ssg` builds a content directory, runs other commands on it, or renders one markdown file. This page mirrors
[man/ssg.1](../man/ssg.1) (`man ssg`). The site format is in [configuration.md](configuration.md),
[templates.md](templates.md) and [markdown.md](markdown.md), or [man/ssg.5](../man/ssg.5) in one page.

```
ssg [build] [--drafts] [--check-links] [--incremental] [--target <name>] [options] [<content-dir>]
ssg init|serve|watch|check|list|clean|doctor [options] [<dir>]
ssg new <path>
ssg plugins list | plugins meta <name>
ssg render [--strict] [--text] [--no-html] [--scripts|--no-scripts] [--no-extensions] <file|->
ssg completions bash|zsh|fish
ssg --help | --version
```

- [Commands](#commands)
- [Global options](#global-options)
- [JSON output](#json-output)
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

## Commands

The first argument is a command only if it is one of the names below; anything else is the
legacy form, and `ssg <dir>` means `ssg build <dir>` throughout 1.x. A directory with a command's
name is written `./list`. `<dir>` defaults to the current directory. Options go after the command.

| command | |
| --- | --- |
| `build [dir]` | Builds the site; see [Building](#building). Takes `--drafts` and `--check-links`. |
| `init [dir]` | Creates `dir` (if needed) with `site.toml`, `skeleton.html`, `templates/base.md`, `index.md` and a `.gitignore` holding `dist/`. Refuses if `dir/site.toml` or any of those files exists, and writes nothing then. |
| `new <path>` | Writes `<path>.md` (`.md` is added if missing) with `title` (from the file name: `hello-world` gives "Hello world") and `date` (today, or from `SOURCE_DATE_EPOCH`) frontmatter. The path is relative to the current directory; absolute paths and `..` are refused. Refuses an existing file. |
| `serve [dir]` | Builds, then serves dist on `127.0.0.1` (`--port`, default 8000, 0 picks a free port; `--open` opens a browser) and rebuilds when a file changes. Open pages reload themselves: ssg adds a small script to served HTML (not to the files in dist). A missing path gets the site's `/404/` page (from `404.md`) or `404.html`, with status 404. Only GET and HEAD. A build error is printed and the server keeps running. Accepts `--drafts`. |
| `watch [dir]` | Builds, then rebuilds whenever a file changes (polling every 300 ms), incrementally: unchanged files are left alone. Accepts `--drafts`. |
| `check [dir]` | Builds into a scratch directory (dist is untouched) and reports every warning of `build --check-links`, plus pages nothing links to (the home page and `404` are exempt), images with no or empty alt text, and `{{keys}}` that nothing defines. Findings are warnings: the exit status is 0 unless `--fail-on-warning`. Keys unknown to the skeleton aren't checked. Accepts `--drafts`. |
| `list [dir]` | Prints each page's URL and source file, sorted by URL. Builds into a scratch directory. Accepts `--drafts`. |
| `clean [dir]` | Empties dist, but only if it is missing, empty or holds the `.ssg-dist` marker; otherwise exit 1 and nothing is touched. |
| `doctor [dir]` | Checks the config, that dist is safe to build into, the templates dir, that the skeleton parses, warns when `dist/.ssg-dist` would be deployed (no `.assetsignore`, `.vercelignore` or `vercel.json` next to the site or dist names it), and checks each `[plugin.<name>]` (found, `--meta` readable, protocol, config keys). Each line starts with `ok`, `warn` or `error`; exit 1 if any is an error. |
| `plugins list` | Lists the `[plugin.<name>]` tables of `./site.toml` (or `--config`) in file order: `name  version  path`, or `disabled`, or why the plugin isn't usable. See [plugins.md](plugins.md). |
| `plugins meta <name>` | Runs the plugin's `--meta` and prints the JSON object on one line. Exit 1 if there is no such table or the plugin can't be run. |
| `render <file\|->` | Unchanged; see [ssg render](#ssg-render). |
| `completions <shell>` | Prints a completion script for `bash`, `zsh` or `fish` on stdout. |

## Global options

Accepted by every command except `render`, after the command name (or anywhere in the legacy
form). They may also come before it: `ssg --json build .`.

| option | |
| --- | --- |
| `--config <file>` | Read this file instead of `<dir>/site.toml`. Relative paths inside it still resolve against `<dir>`; the file itself isn't published. |
| `--output <dir>` | Build into `<dir>` instead of the configured dist (the same safety rules apply). |
| `--base-url <url>` | Override `base_url`. |
| `--verbose` | One stderr line per built page and copied file. |
| `--target <name>` | Also write the deploy files for `cloudflare`, `vercel` or `nginx` (build only; overrides `[deploy] target`). See [deploy.md](deploy.md). An unknown name is a usage error (exit 2). |
| `--force` | Let `--target` regenerate adapter files that already exist, replacing any edits. Without it an existing file is kept as is. |
| `--incremental` | Keep dist instead of emptying it: a file whose bytes are unchanged is not rewritten (its mtime survives), a changed one is replaced, and files the build no longer produces are removed. The result is the same as a clean build; the whole site is still processed in memory, so what is saved is writing and a dist that is never empty mid-build. Only for a dist ssg owns (the `.ssg-dist` marker); with `clean = false` nothing is removed. `serve` and `watch` always build this way. Plugins' `post_build` files are rewritten each time. |
| `--quiet`, `-q` | No summary line and no notes. Warnings and errors still print. |
| `--color auto\|always\|never` | Colour the `warning:` label. `auto` (default) colours only a terminal on stderr and not when `NO_COLOR` is set or `TERM=dumb`. |
| `--json` | A machine-readable result on stdout; see [JSON output](#json-output). Messages stay on stderr. |
| `--fail-on-warning` | Exit 3 if any warning was printed (the work is still done). |
| `--no-plugins` | Don't start the plugins in `site.toml`; the manifest's `plugins` is then empty. |

`--flag=value` works for options that take a value. `--help` / `-h` anywhere prints the usage.

## JSON output

With `--json`, `build`, `check`, `list`, `clean`, `doctor`, `plugins list`, `init` and `new` print one
line of JSON on stdout. Every object has `"schema":1` and `"ok"`; fields are only ever added
within schema 1. On an error (exit 1) the object is `{"schema":1,"ok":false,"error":"..."}`;
usage errors print no JSON.

- `build`: `command`, `dist`, `manifest` (path or `null`), `pages` (`url`, `source`, `output`), `files` (copied, relative to
  dist), `warnings` (`file`, `message`; `file` is `""` when no file is meant, `site.toml:LINE`
  for config warnings) and `timings_ms` (`total`).
- `check`: `pages` (a count) and `warnings`. `list`: `pages` and `warnings`.
- `doctor`: `checks` (`status`, `name`, `message`). `plugins`: `plugins` (`name`, `enabled`, `required`, `resolved`, and when resolved `path`, `version`, `protocol`, `hooks`; `error` if not).
- `clean`: `dist`. `init`: `created`. `new`: `created`.

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
   - the contents of symlinks to directories, and symlinks to files unless `follow_symlinks = true`.
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
whose name starts with `-` as `./-dir`, and one named after a command (`render`, `list`, ...) as
`./render`. Also see [Global options](#global-options).

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
| `--scripts` | Turn the raw-HTML allow-list off: `<script>` and similar elements, `on*` attributes, and `javascript:`, `vbscript:`, `file:` and non-image `data:` URLs pass through. |
| `--no-scripts` | Escape tags and drop attributes that are not on the allow-list, and neutralize those URLs. The default, so it only undoes an earlier `--scripts` or `--strict`. |
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

`--version` is recognised only as the first argument; elsewhere it is an unknown option (exit 2).
`--help` also works after a command.

| option | |
| --- | --- |
| `--help`, `-h` | Usage and an option summary on stdout. |
| `--version` | `ssg 1.0.0-dev (abc1234)` on stdout: the version, then the git commit it was built from (`abc1234-dirty` with uncommitted changes; left out when unknown). Source builds carry the `-dev` suffix; release builds drop it. Later arguments are ignored. |

## Exit status

| status | |
| --- | --- |
| 0 | Success, including builds that printed warnings. |
| 1 | An error: configuration, template, unsafe dist, or file I/O. |
| 2 | Bad usage; the usage text goes to stderr. |
| 3 | Warnings were printed and `--fail-on-warning` was given. |

## Environment

`SOURCE_DATE_EPOCH`: seconds since 1970-01-01 UTC, used for `build_date` instead of the current
time. With it set, the same input builds byte-identical output (the summary line's timing
aside). It must be a decimal integer from 0 to 253402300799 (9999-12-31T23:59:59Z); anything
else is ignored with a warning and the current time is used.

```
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) ssg .
```

`NO_COLOR`: any non-empty value stops `--color auto` from colouring (`--color always` still does).
`TERM=dumb` does the same. No other environment variables are read.

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
| `unknown option <opt>` | Includes `--` and options a command doesn't take (`ssg render --drafts`, `ssg clean --port 1`). |
| `<option> needs a value` | `--config`, `--output`, `--base-url`, `--color` or `--port` ended the arguments. |
| `--color wants auto, always or never`, `--port wants a number from 0 to 65535` | |
| `one content dir only` | |
| `plugins needs list or meta <name>`, `new takes one path`, `completions needs bash, zsh or fish` | |
| `render takes one input` | |
| `render needs a file or -` | |

### Plugins (exit 1)

| message | |
| --- | --- |
| `plugin '<name>': ssg-plugin-<name> is not on PATH ...` | Or `<path> is not an executable file`. With `required = false`, a warning instead. |
| `plugin '<name>': needs plugin protocol N, but this ssg speaks 1` | Also `--meta` problems: it timed out, exited non-zero, printed no JSON, or names another plugin. |
| `plugin '<name>', hook '<hook>': <what>` | `timed out after Ns`, `exited before answering; it exited with status N`, `answered with invalid JSON`, `answered with the wrong id`, `failed: <the plugin's error>`, `the result must be a string`, and for `asset`, `'<path>' is not a path inside dist`. |
| `[plugin.<name>] 'key' must be int, not string` | A config value of the wrong type for the plugin's `--meta`. |
| `[plugin.<name>] timeout must be a whole number of seconds, 1 to 86400` | Also `enabled`, `required` and `path`. |
| `warning: [plugin.<name>] has the key 'k', which <name> does not declare` | |
| `warning: plugin <name>: <text>` | The plugin wrote `<text>` to stderr. |

### site.toml (exit 1)

| message | |
| --- | --- |
| `missing site.toml in <dir>` | Also what a mistyped or non-directory `<dir>` gives. |
| `site.toml must set dist, templates, and skeleton` | |
| `site.toml:<N>: <message>` | Syntax or type error on line N: `'clean' must be a boolean`, `'ignore' must be an array of strings`, `duplicate key 'dist'`, `duplicate table [markdown]` (names the repeated header's line), `'a' is not a table`, `'a' is already a table`, `table [a] was already defined as a value`, `table [a] was already defined by dotted keys`, `table [a.b] can't be extended with dotted keys; ...`, `'plugin' must be a table, like [plugin.<name>]`, `[plugin] holds one table per plugin, like [plugin.x]`, `expected ] after table name`, `only integers are supported`, `leading zeros are not allowed`, `'_' must be between digits`, `integer out of range`, `inline tables are not supported`, `arrays nest too deeply (more than 64 levels)`, `multi-line strings are not supported`, `control characters in strings must be escaped`, `control characters are not allowed in comments`, `unterminated string`, `unknown escape \q`, `expected end of line`, `'text_width' must be between 0 and 100000`, ... See [configuration.md](configuration.md#toml-subset). |
| `warning: site.toml:<N>: unknown key '<k>'` | Also `unknown key '<k>' in [markdown]` and `unknown table [<t>]` (for a header or a dotted key's first part). Keys inside an unknown table are ignored without further warnings. `[plugin.<name>]` tables are accepted without warnings here; each plugin checks its own keys (see below). |

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
| `note: skipping <path> (a symlink; set follow_symlinks = true to copy what it points to)` | `<path>` is a symlink to a file. Hidden by `--quiet`; not a warning. |
| `built <N> pages, copied <M> files in <T> ms (<K> warnings)` | The summary. The part in parentheses only appears when there were warnings. Hidden by `--quiet`. |
