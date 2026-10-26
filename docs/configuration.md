# Configuration and publishing

`site.toml`, what the build publishes, and where it lands. For templates and frontmatter see
[templates.md](templates.md); for markdown syntax see [markdown.md](markdown.md); for flags and exit
codes see [cli.md](cli.md). The same reference, in man form, is `man 5 ssg` ([man/ssg.5](../man/ssg.5)).

- [site.toml](#sitetoml)
  - [Keys](#keys)
  - [\[markdown\]](#markdown)
  - [TOML subset](#toml-subset)
  - [Errors and warnings](#errors-and-warnings)
- [Security: `html` and `scripts`](#security-html-and-scripts)
- [What gets published](#what-gets-published)
- [.assetignore and `ignore`](#assetignore-and-ignore)
- [dist](#dist)
- [Output layout and URLs](#output-layout-and-urls)
- [Drafts](#drafts)
- [sitemap.xml](#sitemapxml)

## site.toml

`<content-dir>/site.toml`. Required: without it the build stops with
`ssg: missing site.toml in <content-dir>`.

```toml
dist = "dist"                       # required
templates = "templates"             # required
skeleton = "skeleton.html"          # required
base_url = "https://isworking.dev"  # optional; enables sitemap.xml
clean = true                        # empty dist before building (only a dist ssg made)
nested_repos = false                # build subdirectories that are git checkouts
ignore = ["*.psd", "drafts/"]       # gitignore patterns, lowest priority

[markdown]
html = true          # raw HTML passes through (sanitized unless scripts = true)
scripts = false      # strip script-capable HTML and URLs
heading_ids = true
autolinks = true
extensions = true
text_width = 80
```

### Keys

Top level:

| key | type | default | meaning |
| --- | --- | --- | --- |
| `dist` | string | required | Output directory. See [dist](#dist) for the rules it must satisfy. |
| `templates` | string | required | Directory holding `{{extend}}`/`{{include}}` targets. Need not exist if nothing uses it. Never published. |
| `skeleton` | string | required | The HTML shell every page is poured into. Must exist: `ssg: missing HTML skeleton: <abs path>`. Never published. |
| `base_url` | string | `""` | Site origin, e.g. `https://isworking.dev`. Trailing `/`s are dropped. Not validated. Only used for `sitemap.xml`; it is not a template variable (`{{base_url}}` stays as written). |
| `clean` | bool | `true` | Empty dist before building, if ssg owns it. See [Cleaning](#cleaning). |
| `nested_repos` | bool | `false` | Also build subdirectories that contain `.git`. See [Nested git checkouts](#nested-git-checkouts). |
| `ignore` | array of strings | `[]` | gitignore patterns, rooted at the content dir, below every `.assetignore` in priority. See [.assetignore and `ignore`](#assetignore-and-ignore). |

Paths (`dist`, `templates`, `skeleton`) are relative to the content dir, or absolute. They are
normalized lexically (`./x/`, `x/` and `x` are the same); symlinks are not resolved at this
stage. `dist`, `templates` and `skeleton` must be non-empty, or:

```
ssg: site.toml must set dist, templates, and skeleton
```

`templates` and `skeleton` may live outside the content dir (shared between sites).

### [markdown]

Applies to every page built. `ssg render` ignores `site.toml` and takes flags instead
([cli.md](cli.md)).

| key | type | default | meaning |
| --- | --- | --- | --- |
| `html` | bool | `true` | Raw HTML (blocks and inline tags) is passed through. `false`: it is escaped and shows as text. |
| `scripts` | bool | `false` | `false`: script-capable HTML and URLs are removed (details [below](#security-html-and-scripts)). `true`: nothing is filtered. |
| `heading_ids` | bool | `true` | Headings get `id="slug"` (GitHub-style, deduplicated). An explicit `{#id}` works either way. Independent of `extensions`. |
| `autolinks` | bool | `true` | Bare `www.x.com`, `https://x.com`, `a@b.c` become links. Has no effect when `extensions = false`. |
| `extensions` | bool | `true` | Tables, strikethrough, task lists, footnotes, definition lists, `:::` containers, `> [!NOTE]` alerts, `==mark==`, `{#id}` heading attributes, bare autolinks. `false` = strict CommonMark. |
| `text_width` | integer | `80` | Wrap column for the `.txt` output. `0` disables wrapping. Range 0..100000, else `site.toml:N: 'text_width' must be between 0 and 100000`. |

Syntax details: [markdown.md](markdown.md).

### TOML subset

ssg reads a subset of TOML. What it accepts:

- `#` comments, on their own line or after a value or table header.
- `key = value`. Keys are bare (`A-Za-z0-9_-`) or quoted (`"key"`, `'key'`).
- `[table]` headers (`[ markdown ]` with spaces is fine). Each table may appear once. Keys
  after a header belong to that table until the next header; there is no way back to the top
  level, so top-level keys must come first.
- Strings: `"basic"` with escapes `\" \\ \n \t \r \b \f \uXXXX \UXXXXXXXX` (no surrogates, max
  U+10FFFF), and `'literal'` (no escapes). Single line only.
- Booleans: `true`, `false` (lowercase).
- Integers: optional `+`/`-`, decimal digits, `_` separators. Looser than TOML: leading zeros
  and stray `_` (`1__0_`) are accepted.
- Arrays of strings: `["a", 'b']`, which may span lines, hold comments, and end with a trailing comma.
- A UTF-8 BOM at the start, and CRLF line endings. Lone CR (classic Mac) line endings are not
  supported (`expected end of line`).

Rejected, each with an error naming the line:

| input | error |
| --- | --- |
| `a.b = 1`, `[a.b]` | `dotted keys are not supported` |
| `[[a]]` | `arrays of tables are not supported` |
| `a = {x = 1}` | `inline tables are not supported` |
| `a = """x"""`, `a = '''x'''` | `multi-line strings are not supported` |
| `a = 1.5`, `a = 1e3`, `a = 07:32:00` | `only integers are supported` |
| `a = [1, 2]` | `arrays may only hold strings` |
| `a = 0x10`, `a = 1979-05-27`, `a = true x` | `expected end of line` |
| `a = True`, `a = inf`, `a =` | `expected a value` |
| `a = 99999999999999999999` | `integer out of range` |
| `a = -` | `expected a number` |
| `a = "\q"` | `unknown escape \q` |
| `a = "\ud800"` | `bad unicode escape` |
| `a = "x` | `unterminated string` |
| `a 1` | `expected = after 'a'` |
| `= 1` | `expected a key` |
| `a = ["a" "b"]` | `expected , or ] in array` |
| `[markdown`, `["a" "b"]` | `expected ] after table name` |
| a key twice in one table | `duplicate key 'a'` |
| a table header twice | `duplicate table [markdown]` (the line number is the one after the repeated header) |

### Errors and warnings

Errors print `ssg: site.toml:<line>: <message>` and exit 1. Nothing is built or written.

- Wrong type for a known key: `site.toml:5: 'clean' must be a boolean` (also `must be a string`,
  `must be an integer`, `must be an array`).
- Unknown top-level key: `ssg: warning: site.toml:5: unknown key 'mystery'`.
- Unknown key in `[markdown]`: `ssg: warning: site.toml:5: unknown key 'x' in [markdown]`.
- Unknown table: `ssg: warning: site.toml:4: unknown table [foo]`. Keys inside it are not checked
  (no type or unknown-key messages, but duplicates are still errors). Table names are
  case-sensitive: `[Markdown]` is unknown.

Warnings don't stop the build and aren't counted in the summary line's `(N warnings)`.

A common slip is putting a top-level key below `[markdown]`:

```
ssg: warning: site.toml:6: unknown key 'skeleton' in [markdown]
ssg: site.toml must set dist, templates, and skeleton
```

## Security: `html` and `scripts`

The defaults (`html = true`, `scripts = false`) let you write HTML in pages while stripping
what can run script. This covers page bodies and everything templates produce, since templates
become part of the page's markdown. The skeleton is not filtered; it is trusted. Frontmatter
values that the skeleton inserts with `{{key}}` are HTML-escaped ([templates.md](templates.md)).

| `html` | `scripts` | raw HTML | link/image URLs in markdown |
| --- | --- | --- | --- |
| `true` | `false` (default) | sanitized (below) | dangerous ones neutralized |
| `true` | `true` | passed through verbatim | passed through |
| `false` | `false` | escaped, shown as text | dangerous ones neutralized |
| `false` | `true` | escaped, shown as text | passed through, `javascript:` included |

Use `scripts = true` only when every page is yours. `html = false` with `scripts = true` still
lets `[x](javascript:...)` links through.

### What `scripts = false` does to raw HTML

Each raw HTML chunk is re-tokenized the way a browser would read it and rebuilt from the tags it
keeps.

- **Escaped tags** (shown as literal text, opening and closing tag alike; any text between
  them stays as text): `applet`, `base`, `embed`, `frame`, `frameset`, `iframe`, `link`, `meta`,
  `noembed`, `noframes`, `noscript`, `object`, `plaintext`, `portal`, `script`, `style`,
  `template`, `textarea`, `title`, `xmp`. Case-insensitive. `<script>alert(1)</script>` renders
  as the text `<script>alert(1)</script>`.
- **Dropped attributes** (on every other tag):
  - any name starting with `on` (`onclick`, `onerror`, ...), and `srcdoc`;
  - names with characters other than letters, digits, `- _ : .`;
  - any attribute whose value, after decoding entities (`&#x09;`, `&#106;`, numeric refs without
    `;`) and removing whitespace and control characters, contains `javascript:`, `vbscript:` or
    `livescript:` anywhere, case-insensitively. This is deliberately blunt:
    `title="see javascript: docs"` is dropped too;
  - a value that is a dangerous URL (next list);
  - `style` values containing `expression(`, `behavior:` or `-moz-binding`.
- **Comments** (`<!-- ... -->`) are removed entirely.
- `<!DOCTYPE>`, `<?...?>`, `<![CDATA[`, and any `<` not starting a well-formed tag: the `<` is
  escaped.
- An **unclosed tag** (`<i class="x` with no `>`) turns the rest of that HTML chunk into escaped
  text, since a browser would swallow it into the tag.
- Kept attributes are rewritten as `name="value"` with the value re-escaped.

Everything else passes: `div`, `span`, `img`, `a`, `details`, `video`, `svg`, `math`, `form`,
`button`, `input`, inline `style="..."` attributes. This is a deny-list aimed at script execution
(XSS), not a full sandbox: forms, remote images and CSS `url()` (tracking) are allowed.

### Dangerous URLs

With `scripts = false`, a URL is dangerous if, after skipping leading whitespace/control bytes and
ignoring whitespace and control characters inside it, it starts (case-insensitively) with:

- `javascript:`, `vbscript:`, `file:`
- `data:`, except `data:image/png`, `data:image/gif`, `data:image/jpeg`, `data:image/webp`.
  `data:image/svg+xml` is dangerous (SVG can carry script).

Where they appear:

| source | result |
| --- | --- |
| `[x](javascript:alert(1))`, `<javascript:alert(1)>` | `<a href="#">` |
| `![x](javascript:x)` | `<img src="" ...>` |
| raw `<a href="jav&#x09;ascript:...">` | `<a>` (attribute dropped) |
| raw `<img src="data:image/svg+xml,...">` | `<img>` |
| raw `<form action="javascript:...">` | `<form>` |

In the `.txt` output a link with a dangerous URL is reduced to its text.

### `html = false`

All raw HTML, block and inline, is escaped and appears as text. An HTML block is shown inside a
`<p>`. Markdown itself (including links, images and autolinks) is unaffected.

## What gets published

Every `.md` file becomes a page (`.html`, `.md`, `.txt`, see
[Output layout](#output-layout-and-urls)). Every other regular file is copied byte for byte to the
same relative path in dist. Empty directories are not created.

### Walk order

One depth-first walk from the content dir. Each directory's entries are sorted by name in byte
order (`B` before `a`); a subdirectory is walked when it is reached. For each directory:

1. Its `.assetignore`, if any, is loaded; its rules apply to that directory's subtree.
2. Each entry is checked, in this order, and skipped at the first match:
   1. Hidden: the name starts with `.`. Exception: a **directory** named `.well-known`, at any depth.
   2. The dist dir, the templates dir, or the skeleton file (exact path match).
   3. `site.toml` in the content root (only there: `sub/site.toml` is an ordinary file).
   4. Ignored by `ignore = [...]` or an `.assetignore` rule.
   5. A directory with a `.git` entry (dir, file or symlink), unless `nested_repos = true`.
   6. A symlinked directory: not followed, skipped silently. (Step 5 looks through the link, so a
      symlink to a git checkout gets the nested-checkout note unless `nested_repos = true`.)
   7. Anything that isn't a regular file (or a symlink to one): FIFOs, sockets, dangling symlinks.
3. What remains: `*.md` (case-sensitive: `README.MD` is an asset) is a page, the rest are assets.

So the following are never published, whatever the rules say (`!.htaccess` does not bring a
hidden file back):

- `.git/`, `.github/`, `.gitignore`, `.env`, `.wrangler/`, `.DS_Store`, `.assetignore`,
  `.htaccess`, `.nojekyll`, ... at any depth. Inside `.well-known/`, hidden names are still
  skipped (`.well-known/.x`).
- `site.toml` (root), the skeleton, the templates dir and everything in it, the dist dir.

Files like `_headers` and `_redirects` are not hidden and are published.

If the templates dir or skeleton is outside the content dir, there is nothing to exclude. If
the skeleton lives in a subdirectory (`layout/skeleton.html`), only that file is excluded; the
rest of `layout/` is published.

### Symlinks

| entry | behaviour |
| --- | --- |
| symlink to a file | followed: the target's content is copied (or rendered, if the link name ends in `.md`) |
| symlink to a directory | skipped, not followed, no message (a link to a git checkout gets the nested-checkout note unless `nested_repos = true`) |
| dangling symlink | skipped |
| symlinks inside dist | removed before anything is written over them, never written through (also with `clean = false`) |
| dist itself is a symlink | allowed; output goes through it; never cleaned (see [dist](#dist)) |

### Nested git checkouts

A subdirectory containing `.git` is another project (for example CI checking the builder out
into `./builder`). By default it is skipped, markdown included, and a note is printed unless
`--quiet`:

```
ssg: note: skipping builder/ (a nested git checkout; set nested_repos = true to build it)
```

The note isn't counted as a warning. Listing the directory in `.assetignore` (`builder/`)
skips it silently, since ignore rules are checked first. `nested_repos = true` builds such
directories like any other (their `.git` is still hidden; their `site.toml` is an ordinary asset;
their `.md` files are pages and must render). The content dir's own `.git` is unaffected either
way: it's hidden.

## .assetignore and `ignore`

Exclude files with gitignore patterns, in `.assetignore` files (in the content root or any
subdirectory) or in `ignore = [...]` in `site.toml`. Both apply to pages and assets: an ignored
`.md` is not built, doesn't appear in `{{children}}`, and doesn't count when deciding
`foo.md` vs `foo/index.md`.

The pattern semantics are git's (wildmatch with pathname matching), checked against
`git check-ignore` on several thousand patterns.

### Syntax

| pattern | matches |
| --- | --- |
| blank line | nothing |
| `# text` | comment. `\#x` matches a file named `#x` |
| `!pattern` | re-includes what an earlier rule excluded. `\!x` matches a file named `!x` |
| `name` | `name` (file or directory) at any depth below the `.assetignore`'s directory |
| `name/` | directories only (a file called `name` is kept) |
| `/name`, `a/b`, `a/*.txt` | a `/` at the start or in the middle anchors the pattern to the `.assetignore`'s directory |
| `*` | anything except `/` |
| `?` | one character except `/` |
| `[a-z]`, `[!a-z]`, `[^a-z]` | one character in / not in the set; `\` escapes inside |
| `[[:alpha:]]` | POSIX classes `alnum alpha blank cntrl digit graph lower print punct space upper xdigit`, ASCII only |
| `**/x` | `x` at any depth |
| `a/**` | everything inside `a` |
| `a/**/b` | `a/b`, `a/x/b`, `a/x/y/b` |
| `\x` | literal `x` |

Line handling:

- Trailing spaces are removed unless escaped: `foo\ ` keeps one space; `foo\\ ` (escaped
  backslash, then a space) keeps none. Leading spaces are significant.
- A trailing CR is stripped (CRLF files work). A UTF-8 BOM at the start is skipped.
- Only one leading and one trailing `/` are stripped: `foo//` does not match the directory `foo`.
- A malformed pattern never matches: unterminated `[`, unknown `[:name:]`, trailing lone `\`.
- Matching is case-sensitive and linear in time; no pattern can hang the build.

### Precedence

Rules are evaluated in order and the last matching rule wins:

1. `ignore = [...]` from `site.toml` (as if it were an `.assetignore` in the content root, read first);
2. the root `.assetignore`;
3. deeper `.assetignore` files, each after its parent's.

So a deeper file can override a shallower one, and any `.assetignore` can override `ignore`
(`ignore = ["x.txt"]` + `.assetignore: !x.txt` publishes `x.txt`), but not the reverse.

As in git, nothing under an excluded directory can be re-included, and an excluded directory's
`.assetignore` is never read:

```gitignore
# No effect on ok.txt: private/ is never entered.
private/
!private/ok.txt

# Publishes assets/logo.svg: assets/ itself is walked and its entries are excluded one by one.
# assets/img/logo.svg stays excluded, because assets/img/ is excluded as a directory.
assets/**
!assets/logo.svg
```

### Examples

```gitignore
# content-root/.assetignore. Comments go on their own lines: "x  # y" is one pattern.
# Any worker.js, at any depth:
worker.js
# Only the one in the root:
/wrangler.jsonc
# CI checkout (also skipped as a nested git checkout, but this keeps the build quiet):
builder/
*.psd
!keep.psd
# Pages under drafts/, at any depth; its images still ship:
drafts/**/*.md
# Any directory named notes:
notes/
```

```toml
# site.toml
ignore = ["*.psd", "/scratch/", "**/tmp/"]
```

`a/.assetignore` containing `/b/` excludes `a/b/` only; `b/` and `a/x/b/` are published.

### Differences from git

- Matching is always case-sensitive. git on macOS and Windows defaults to `core.ignorecase =
  true`, so `*.PNG` there also ignores `a.png`; here it doesn't.
- Hidden names are excluded before any rule is consulted; `!` cannot publish them.
- `ignore = [...]` plays the part of `.git/info/exclude`: lowest priority. An entry starting with
  `#` is a comment and does nothing.
- There is no tracked/untracked distinction: rules apply to every file in the tree.

## dist

### Safety rules

Checked before anything is built or written; each is an error (exit 1) naming the resolved absolute
path. Paths are compared after resolving symlinks.

| condition | error |
| --- | --- |
| dist is the content dir or an ancestor of it (`"."`, `".."`) | `dist (<path>) must not be the content dir or contain it` |
| dist is or contains the templates dir | `dist (<path>) must not be or contain the templates dir` |
| dist contains the skeleton | `dist (<path>) must not contain the skeleton` |
| a path component of dist below what it shares with the content dir starts with `.` | `dist (<path>) must not be inside a hidden dir (.git)` |

The hidden-dir rule rejects `.git/out`, `../.out`, and also an absolute dist such as
`~/.cache/site` when the content dir is elsewhere. Components shared with the content dir don't
count (a content dir under `~/.local/site` may use `dist = "dist"`).

dist may be inside the content dir (it is then excluded from the walk) or anywhere outside it.
A dist that is a regular file fails with a filesystem error (`File exists`).

### Ownership: the .ssg-dist marker

ssg only empties a directory it created. The marker is a file `dist/.ssg-dist`
(content: `Build output of ssg, which empties this directory before each build.`).

At the start of each build, dist is **owned** if it:

- doesn't exist, or
- is an empty directory, or
- is a directory (not a symlink) that holds `.ssg-dist`.

An owned dist gets the marker (re)written, with or without `clean`. A dist that isn't owned is
never marked, so a mistyped `dist = "posts"` is never wiped on a later build either.

The marker is ordinary output: it is deployed with the site. `--check-links` treats links to it as
broken.

### Cleaning

| dist at build start | `clean = true` (default) | `clean = false` |
| --- | --- | --- |
| missing or empty | created, marked, built | created, marked, built |
| has `.ssg-dist` | emptied, marked, built | marked, built over; stale files stay |
| non-empty, no marker | warning; built over; not marked; stale files stay | built over silently; not marked |
| a symlink to a directory | warning (`not a plain directory`); built over through the link; never cleaned | built over through the link |

The warnings:

```
ssg: warning: not cleaning /abs/dist: it wasn't created by ssg (no .ssg-dist); delete it once by hand
ssg: warning: not cleaning /abs/dist: not a plain directory
```

Emptying removes every entry in dist, the marker included, without following symlinks; then the
marker is written again. "Built over" means files are written in place: pages and assets
overwrite same-named files; anything else already in dist stays.

### Migrating a dist from an older ssg

Older versions didn't write `.ssg-dist`, so their dist (for example `example/dist`, or a
site's local `dist/`) is not owned: every build warns `not cleaning ... (no .ssg-dist)` and pages
you deleted keep being deployed. Delete it once:

```sh
rm -rf dist && ssg .
```

From then on the build creates and owns it. CI jobs that build into a fresh checkout are
unaffected.

## Output layout and URLs

Each page writes three files side by side: `.html` (skeleton + rendered page), `.md` (the page
after templates, frontmatter removed) and `.txt` (plain text, wrapped at `text_width`). Assets
keep their relative path.

`foo.md` is published as a directory index, unless `foo/index.md` is also a page:

| source | output | `{{url}}` |
| --- | --- | --- |
| `index.md` | `index.html`, `index.md`, `index.txt` | `/` |
| `about.md` | `about/index.{html,md,txt}` | `/about/` |
| `blog/index.md` | `blog/index.{html,md,txt}` | `/blog/` |
| `blog/post.md` | `blog/post/index.{html,md,txt}` | `/blog/post/` |
| `foo.md` and `foo/index.md` | `foo.{html,md,txt}` and `foo/index.{html,md,txt}` | `/foo` and `/foo/` |
| `a b.md`, `ü.md` | `a b/index.*`, `ü/index.*` | `/a%20b/`, `/%C3%BC/` |
| `style.css`, `img/x.png` | same path | none |

- In URLs, every byte outside `A-Z a-z 0-9 - . _ ~ /` is percent-encoded (UTF-8, uppercase hex).
  File names in dist are not encoded.
- `foo/index.md` only counts if it is a page: an ignored or draft `foo/index.md` leaves `foo.md`
  at `foo/index.*`.
- A directory without `index.md` gets no index page: `notes/a.md` is `/notes/a/`, and `/notes/`
  has nothing unless you write `notes/index.md`.

With a server that picks the extension by `Accept` (`text/html` → `.html`, `text/markdown` →
`.md`, else `.txt`), mapping `dir/` to `dir/index.EXT` and `/foo` to `foo.EXT`, or a redirect to
`/foo/` when that doesn't exist:

| request | `about.md` alone | `foo.md` + `foo/index.md` |
| --- | --- | --- |
| `/about/`, `/foo/` | `about/index.EXT` | `foo/index.EXT` |
| `/about`, `/foo` | 301 to `/about/` | `foo.EXT` |

### Collisions

When two sources write the same output path, the later one wins and a warning names both:

```
ssg: warning: foo.txt and foo.md both write foo.txt (the latter wins)
ssg: warning: bar/index.html and bar.md both write bar/index.html (the latter wins)
```

Assets are copied before pages are rendered, so a page always beats an asset. Typical causes: an
asset `foo.txt` or `foo.html` next to `foo.md` + `foo/index.md`; an asset `bar/index.html` next to
`bar.md`. A content-root `sitemap.xml` and the generated one are covered under
[sitemap.xml](#sitemapxml).

On a case-insensitive file system (macOS, Windows defaults), outputs that differ only in case
(`Foo.md` → `Foo/index.html`, `foo/index.md` → `foo/index.html`) overwrite each other **without** a
warning. Keep names that differ only in case out of the content dir.

## Drafts

A page is a draft when its `draft` value is truthy: set, and after trimming not empty, `false`,
`0`, `no` or `off` (any case). So `draft: true`, `draft: "yes"` and `draft: 1` are drafts;
`draft: "Off"` and `draft:` are not.

Without `--drafts`, a draft is left out entirely: no output, not in `{{children}}`,
prev/next, or the sitemap, and it doesn't count for `foo.md` vs `foo/index.md`.

- `draft` set in the page's own frontmatter (and not containing `{{`): the page isn't even
  rendered, so a draft with a broken template doesn't fail the build.
- `draft` set by a template, or computed (`draft: "{{wip}}"`): the page is rendered once to find
  out, then dropped. Template errors in it still fail the build.

With `--drafts` ([cli.md](cli.md)), drafts are built like any page and appear in nav and the sitemap.

## sitemap.xml

Written to `dist/sitemap.xml` when `base_url` is non-empty.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
  <url><loc>https://example.com/</loc><lastmod>2026-03-04</lastmod></url>
  <url><loc>https://example.com/a/</loc><lastmod>2026-03-05</lastmod></url>
  <url><loc>https://example.com/nodate/</loc></url>
</urlset>
```

- One `<url>` per built page, sorted by URL (byte order). `<loc>` is `base_url` + `{{url}}`,
  XML-escaped. Assets are not listed.
- `<lastmod>` is the first 10 characters of the page's `date` when it looks like `YYYY-MM-DD`,
  optionally followed by `T` or a space and anything else (`2026-01-02T10:00` → `2026-01-02`).
  Otherwise (`2026-1-2`) there is no `<lastmod>`.
- A page whose final `sitemap` value is falsy (`false`, `0`, `no`, `off`, or empty) is left out.
  The value may come from a template.
- Drafts are listed only with `--drafts`.
- If the content root has its own `sitemap.xml` (not ignored), it is copied instead and the build
  warns: `ssg: warning: sitemap.xml exists in the content dir; not generating one`.

`base_url` should be the origin the site is served from at `/`. A path in it
(`https://x.dev/blog`) is prefixed to sitemap entries only; `{{url}}` and the links ssg generates
stay root-relative.
