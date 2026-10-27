# Configuration and publishing

`site.toml`, what the build publishes, and where it lands. For templates and frontmatter see
[templates.md](templates.md); for markdown syntax see [markdown.md](markdown.md); for flags and exit
codes see [cli.md](cli.md). The same reference, in man form, is `man 5 ssg` ([man/ssg.5](../man/ssg.5)).

- [site.toml](#sitetoml)
  - [Keys](#keys)
  - [\[taxonomy.&lt;name&gt;\]](#taxonomyname)
  - [\[feed\]](#feed)
  - [\[robots\]](#robots)
  - [\[deploy\] and \[headers\]](#deploy-and-headers)
  - [\[markdown\]](#markdown)
  - [TOML subset](#toml-subset)
  - [Plugin tables](#plugin-tables)
  - [Errors and warnings](#errors-and-warnings)
- [Security: `html` and `scripts`](#security-html-and-scripts)
- [What gets published](#what-gets-published)
- [.assetignore and `ignore`](#assetignore-and-ignore)
- [dist](#dist)
- [Output layout and URLs](#output-layout-and-urls)
- [Drafts](#drafts)
- [Scheduled pages](#scheduled-pages)
- [Data files](#data-files)
- [Pagination](#pagination)
- [Redirects](#redirects)
- [sitemap.xml](#sitemapxml)
- [manifest.json](#manifestjson)

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
follow_symlinks = false             # copy files that are symlinks to a file
ignore = ["*.psd", "drafts/"]       # gitignore patterns, lowest priority

[css]
minify = false       # minify .css assets
inline_limit = 16384 # warn above this size for {{inline}}

[markdown]
html = true          # raw HTML passes through (allow-listed unless scripts = true)
scripts = false      # allow-list raw HTML, neutralize script-capable URLs
allow_tags = []      # extra tags to allow, e.g. ["my-card"]
allow_attrs = []     # extra attributes to allow on any tag, e.g. ["style"]
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
| `manifest` | string or `false` | beside dist | Where `manifest.json` is written, relative to the content dir (or absolute); `false` turns it off. See [manifest.json](#manifestjson). |
| `clean` | bool | `true` | Empty dist before building, if ssg owns it. See [Cleaning](#cleaning). |
| `future` | bool | `false` | Build pages whose `date` is after the build date. See [Scheduled pages](#scheduled-pages). |
| `redirect_pages` | bool | `false` | Also write a meta-refresh HTML page at each redirect's old URL. See [Redirects](#redirects). |
| `nested_repos` | bool | `false` | Also build subdirectories that contain `.git`. See [Nested git checkouts](#nested-git-checkouts). |
| `follow_symlinks` | bool | `false` | Copy (or render) files that are symlinks to a file. Off, they are skipped with a note. See [Symlinks](#symlinks). |
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
| `scripts` | bool | `false` | `false`: raw HTML goes through an allow-list and script-capable URLs are removed (details [below](#security-html-and-scripts)). `true`: nothing is filtered. |
| `allow_tags` | array of strings | `[]` | Tag names to add to the raw-HTML allow-list (case-insensitive plain names, e.g. `["my-card"]`). Only matters with `html = true`, `scripts = false`. |
| `allow_attrs` | array of strings | `[]` | Attribute names to allow on every allowed tag, e.g. `["style"]`. Values are still checked for `javascript:` and other dangerous URLs. |
| `heading_ids` | bool | `true` | Headings get `id="slug"` (GitHub-style, deduplicated). An explicit `{#id}` works either way. Independent of `extensions`. |
| `autolinks` | bool | `true` | Bare `www.x.com`, `https://x.com`, `a@b.c` become links. Has no effect when `extensions = false`. |
| `extensions` | bool | `true` | Tables, strikethrough, task lists, footnotes, definition lists, `:::` containers, `> [!NOTE]` alerts, `==mark==`, `{#id}` heading attributes, bare autolinks. `false` = strict CommonMark. |
| `text_width` | integer | `80` | Wrap column for the `.txt` output. `0` disables wrapping. Range 0..100000, else `site.toml:N: 'text_width' must be between 0 and 100000`. |

Syntax details: [markdown.md](markdown.md).

### TOML subset

ssg reads a subset of TOML. What it accepts:

- `#` comments, on their own line or after a value or table header. Like strings, they may
  not hold raw control characters other than tab.
- `key = value`. Keys are bare (`A-Za-z0-9_-`) or quoted (`"key"`, `'key'`).
- Dotted keys: `markdown.html = false` is the same as `html = false` under `[markdown]`. Each part
  is bare or quoted (`a."b.c"` has two parts), and spaces around the dots are fine.
- `[table]` headers, which may be dotted too: `[plugin.search]`, `[ markdown ]`. Keys after a
  header belong to that table until the next header; there is no way back to the top level, so
  top-level keys must come first. A header's parents need no header of their own, and may get
  one later: `[a.b]` then `[a]` is fine.
- Strings: `"basic"` with escapes `\" \\ \n \t \r \b \f \uXXXX \UXXXXXXXX` (no surrogates, max
  U+10FFFF), and `'literal'` (no escapes). Single line only. Raw control characters (other than
  tab, and including DEL) are rejected; in a basic string, write them as escapes.
- Booleans: `true`, `false` (lowercase).
- Integers: decimal, with an optional `+`/`-`, and `_` only between two digits (`1_000`). No
  leading zeros (`0` and `-0` are fine). The full 64-bit range, -9223372036854775808 to
  9223372036854775807.
- Arrays of any of these values, including other arrays: `["a", 'b']`, `[1, [2, 3], true, []]`.
  They may span lines, hold comments, and end with a trailing comma. Known keys still check what
  they hold: `ignore` must be an array of strings. Arrays nest at most 64 deep.
- A UTF-8 BOM at the start, and CRLF line endings. Lone CR (classic Mac) line endings are not
  supported (`expected end of line`).

Each name is defined once. A key and a table can't share a name, a `[table]` header can't repeat,
and a table made by dotted keys can't get a header later (or the other way round).

Rejected, each with an error naming the line:

| input | error |
| --- | --- |
| `[[a]]` | `arrays of tables are not supported` |
| `a = {x = 1}` | `inline tables are not supported` |
| `a = """x"""`, `a = '''x'''` | `multi-line strings are not supported` |
| `a = 1.5`, `a = 1e3`, `a = inf`, `a = nan` | `only integers are supported` |
| `a = 1979-05-27` | `dates and times are not supported` |
| `a = 0x10`, `a = 0o7`, `a = 0b1` | `only decimal integers are supported` |
| `a = 07`, `a = 0_1`, `a = 07:32:00` | `leading zeros are not allowed` |
| `a = 1__0`, `a = 1_` | `'_' must be between digits` |
| `a = 99999999999999999999` | `integer out of range` |
| `a = -` | `expected a number` |
| `a = true x` | `expected end of line` |
| `a = True`, `a = _1`, `a =` | `expected a value` |
| `a = "\q"` | `unknown escape \q` |
| `a = "\ud800"` | `bad unicode escape` |
| `a = "x` | `unterminated string` |
| a raw control character (other than tab) in a string | `control characters in strings must be escaped` |
| a raw control character (other than tab) in a comment | `control characters are not allowed in comments` |
| `a 1` | `expected = after 'a'` |
| `= 1` | `expected a key` |
| `a = ["a" "b"]` | `expected , or ] in array` |
| arrays nested more than 64 deep | `arrays nest too deeply (more than 64 levels)` |
| `[markdown`, `["a" "b"]` | `expected ] after table name` |
| `a = 1` twice in one table, or `a.b = 1` twice | `duplicate key 'a'`, `duplicate key 'a.b'` |
| a table header twice | `duplicate table [markdown]` (on the repeated header's line) |
| `a = 1` then `a.b = 2` | `'a' is not a table` |
| `a.b = 1` then `a = 2` | `'a' is already a table` |
| `a = 1` then `[a]` | `table [a] was already defined as a value` |
| `a.b = 1` then `[a]` | `table [a] was already defined by dotted keys` |
| `[a.b]`, then `[a]` with `b.c = 1` | `table [a.b] can't be extended with dotted keys; add the key under its header` |
| `[a.b.c]`, then `[a]` with `b.d = 1` | `table [a.b] can't be extended with dotted keys; give it a [a.b] header` |

### [taxonomy.&lt;name&gt;]

A taxonomy groups pages by a comma-separated frontmatter value, such as `tags: "rust, web dev"`.
Each `[taxonomy.<name>]` table (the name holds letters, digits, `-` and `_`; `[taxonomy.tags]`
alone is enough) generates an index page and one page per term:

| key | type | default | meaning |
| --- | --- | --- | --- |
| `key` | string | the name | The frontmatter key to read |
| `path` | string | the name | Where the pages go: `/<path>/` and `/<path>/<term>/` (a relative path, may nest: `topics/tags`) |
| `title` | string | the name, capitalised | The index page's title and `{{taxonomy_title}}` |

- Terms are split on `,` and trimmed. The URL part is the term lowercased, with every run of
  characters other than letters, digits and non-ASCII bytes turned into one `-`: `Web Dev` is
  `web-dev`, `C++ & Rust` is `c-rust`. Terms with the same URL part are one term, named after the
  first page that has it (in source order). A term with nothing left for a URL is skipped with a
  warning. The value is read after the page's templates, in the first pass.
- The pages are built from `templates/taxonomy.md` (the index) and `templates/term.md`: ordinary
  pages with frontmatter, `{{extend}}` and so on. Without them the page is only the list.
  Keys for both: `taxonomy`, `taxonomy_title`, `term_count` (terms on the index, pages on a
  term). The index also gets `{{terms}}`, a markdown list `- [Name](/tags/slug/) (count)` sorted by
  URL part. A term page gets `term`, `term_slug`, `term_url`, `taxonomy_url` and `{{pages}}`, a
  list of the pages with the term, newest first (undated pages after the dated ones, by URL). Both
  pages' `title` defaults to the taxonomy title or the term name, unless the template sets one.
- Every page also gets `{{<name>_links}}` (`{{tags_links}}`): the list of that page's own terms,
  empty when it has none.
- Nothing is generated when no page has a term. A page already at one of the URLs wins, with a
  warning. Generated pages are in the sitemap and the manifest (their `source` is the
  virtual `tags/index.md`), but not in `{{children}}`, navigation or the feed.

### [feed]

An Atom 1.0 feed, `feed.xml`, is written when the table is present (`[feed]` alone is enough).
It needs `base_url`, because entries carry absolute URLs; without one the build warns and writes
no feed.

| key | type | default | meaning |
| --- | --- | --- | --- |
| `section` | string | `""` | Only pages under this directory (`posts` gives `/posts/...`, not the index page `/posts/` itself); empty means every dated page |
| `title` | string | host of `base_url` | The feed's `<title>` |
| `author` | string | the title | Feed-level `<author>`; Atom requires one |
| `limit` | integer | `20` | Newest entries kept, 1 to 10000 |

- Entries are the built pages with a `date` of the form `YYYY-MM-DD[...]`, newest first (ties by
  URL). `404`, generated pages and pages with a falsy `feed` value (`feed: "false"`) are left
  out. Scheduled pages and drafts are not built, so they are not listed.
- An entry's `<title>` is the page's link title (`nav_title`, `page_title`, `title`, then the file name, as in navigation), `<updated>` its date
  (time of day dropped), `<summary>` its `description` if it has one, and `<author>` its
  `author` if that differs from the feed's. The feed's `<updated>` is the newest entry's date.
- `{{feed_url}}` is defined on every page while a feed is written, for
  `<link rel="alternate" type="application/atom+xml" href="{{feed_url}}">` in the skeleton.
- A `feed.xml` in the content dir wins, with a warning.

### [robots]

`robots.txt` is written to dist on every build, allowing everything, unless the content dir has
a `robots.txt` of its own (which is copied and wins, silently: it is only a default; with a
`[robots]` table present it wins with a warning).

| key | type | default | meaning |
| --- | --- | --- | --- |
| `enabled` | bool | `true` | `false` writes no `robots.txt` |
| `disallow` | array of strings | `[]` | Paths (each starting with `/`) to disallow for all robots, instead of `Allow: /` |
| `sitemap` | bool | `true` | Add `Sitemap: <base_url>/sitemap.xml` when `base_url` is set and a sitemap is written |

```
User-agent: *
Disallow: /drafts/

Sitemap: https://example.com/sitemap.xml
```

### [deploy] and [headers]

Hosting adapters; see [deploy.md](deploy.md) for what they write.

```toml
[deploy]
target = "cloudflare"       # or "vercel", "nginx"; --target overrides it

[headers]
link = true                 # Link: <.../index.md>; rel="alternate" on HTML pages
format = true               # X-SSG-Format: html, md or txt
version = false             # X-SSG-Version (opt-in)
page_cache = "public, max-age=300"   # Cache-Control for pages; "" sends none
```

The adapter files are written only by `ssg build`, never by `check`, `serve` or `list`.

### [css]

CSS is copied as is unless you opt in; there is no preprocessor. See
[`{{asset}}` and `{{inline}}`](templates.md#asset-and-inline) for the template side.

| key | type | default | meaning |
| --- | --- | --- | --- |
| `minify` | bool | `false` | Minify every `.css` asset before it is published, fingerprinted or inlined |
| `inline_limit` | int | `16384` | Warn when an `{{inline}}`d stylesheet is larger than this many bytes; `0` never warns |

The minifier is conservative: it drops comments (keeping `/*! ... */`), collapses whitespace,
trims it around `{ } ; ,` and after `:` and `(`, and removes the `;` before `}`. Strings,
`url(...)` and escapes are copied as written, and spaces around `+` and `-` in `calc()` are
never touched. A space before `:` stays, since `a :hover` and `a:hover` differ. Sass, PostCSS,
autoprefixing and `@import` bundling are out of scope; a plugin can do those.

### Plugin tables

Each `[plugin.<name>]` table declares a plugin: ssg runs `ssg-plugin-<name>` (from `PATH`, or
`path`), in the order the tables appear. `enabled`, `path`, `required` and `timeout` are ssg's;
every other key is the plugin's own config and may be nested. The protocol is frozen (see
[stability.md](stability.md)). See [plugins.md](plugins.md).

```toml
[plugin.search]
path = "bin/ssg-plugin-search"
max_chars = 2000
exclude = ["/404/", "/drafts/"]
```

`plugin` itself only holds those tables:

| input | error |
| --- | --- |
| `plugin = 1` | `'plugin' must be a table, like [plugin.<name>]` |
| `plugin.x = 1`, or `x = 1` under `[plugin]` | `[plugin] holds one table per plugin, like [plugin.x]` |

### Errors and warnings

Errors print `ssg: site.toml:<line>: <message>` and exit 1. Nothing is built or written.

- Wrong type for a known key: `site.toml:5: 'clean' must be a boolean` (also `must be a string`,
  `must be an integer`, `must be an array`).
- Unknown top-level key: `ssg: warning: site.toml:5: unknown key 'mystery'`.
- Unknown key in `[markdown]`: `ssg: warning: site.toml:5: unknown key 'x' in [markdown]`.
- Unknown table: `ssg: warning: site.toml:4: unknown table [foo]`, once per table, whether it is
  named by a header (`[foo]`, `[markdown.sub]`) or by a dotted key (`foo.bar = 1` warns about
  `[foo]`). Keys and tables inside it are not checked (no type or unknown-key messages, but
  duplicates and conflicts are still errors). Table names are case-sensitive: `[Markdown]` is
  unknown. `[plugin.<name>]` tables are never unknown; see [Plugin tables](#plugin-tables).

Warnings don't stop the build and aren't counted in the summary line's `(N warnings)`.

A common slip is putting a top-level key below `[markdown]`:

```
ssg: warning: site.toml:6: unknown key 'skeleton' in [markdown]
ssg: site.toml must set dist, templates, and skeleton
```

## Security: `html` and `scripts`

The defaults (`html = true`, `scripts = false`) let you write ordinary HTML in pages while
keeping everything else out: raw HTML goes through an allow-list of tags and attributes. This covers page bodies and everything templates produce, since templates
become part of the page's markdown. The skeleton is not filtered; it is trusted. Frontmatter
values that the skeleton inserts with `{{key}}` are HTML-escaped ([templates.md](templates.md)).

| `html` | `scripts` | raw HTML | link/image URLs in markdown |
| --- | --- | --- | --- |
| `true` | `false` (default) | allow-listed (below) | dangerous ones neutralized |
| `true` | `true` | passed through verbatim | passed through |
| `false` | `false` | escaped, shown as text | dangerous ones neutralized |
| `false` | `true` | escaped, shown as text | passed through, `javascript:` included |

Use `scripts = true` only when every page is yours. `html = false` with `scripts = true` still
lets `[x](javascript:...)` links through.

### What `scripts = false` does to raw HTML

Each raw HTML chunk is re-tokenized the way a browser would read it and rebuilt from the tags
and attributes on the allow-list. Nothing else gets through, including things invented after
this list was written.

- **Allowed tags**: `a abbr address article aside audio b bdi bdo blockquote br caption cite
  code col colgroup data dd del details dfn div dl dt em figcaption figure footer h1`-`h6 header
  hgroup hr i img ins kbd li main mark nav ol p picture pre q rp rt ruby s samp section small
  source span strong sub summary sup table tbody td tfoot th thead time tr track u ul var video
  wbr`. Add more with `allow_tags`.
- **Escaped tags** (shown as literal text, opening and closing tag alike; any text between them
  stays as text): every tag not on the list, e.g. `script`, `style`, `iframe`, `object`, `form`,
  `input`, `button`, `svg`, `math` and custom elements. Case-insensitive.
  `<script>alert(1)</script>` renders as the text `<script>alert(1)</script>`.
- **Allowed attributes**: on every tag `class dir hidden id lang role title translate` and any
  `aria-*` or `data-*`; plus per tag: `a` (`href hreflang rel target type`), `img` (`alt
  decoding height loading sizes src srcset width`), `audio`/`video` (`src controls loop muted
  preload`, and for `video` `poster width height playsinline`), `source`, `track`, `td`/`th`
  (`colspan rowspan headers abbr align`, `scope` on `th`), `col`/`colgroup` (`span`), `ol`
  (`start reversed type`), `li` (`value`), `time` (`datetime`), `del`/`ins` (`cite datetime`),
  `blockquote`/`q` (`cite`), `data` (`value`), `details` (`open`). `style` is not on the list;
  add it, or any other name, with `allow_attrs`.
- **Dropped attributes** (everything not allowed above, and on allowed ones):
  - any name starting with `on` (`onclick`, `onerror`, ...), and `srcdoc`, even if listed in
    `allow_attrs`;
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

Remote images and links are allowed, so this is not a privacy sandbox. `allow_tags` can name any
tag except that you should think twice before adding `script`, `style`, `iframe`, `object` or
`form`; for a whole page of trusted HTML use `scripts = true` instead.

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
   8. A symlink to a file, unless `follow_symlinks = true`: skipped with a note.
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
| symlink to a file | skipped with `note: skipping <path> (a symlink; ...)` (hidden by `--quiet`). With `follow_symlinks = true`: the target's content is copied (or rendered, if the link name ends in `.md`) |
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

### Scheduled pages

A page whose `date` is later than the build date is held back exactly like a draft: no output,
not in `{{children}}`, not in the sitemap. The rule applies to dates written `YYYY-MM-DD`
(optionally followed by `T` or a space and a time); the time is ignored, so a page dated
`2030-01-01T23:00` goes out on the build of 2030-01-01. Any other `date` value is not a date and
never schedules a page. The build date is today's UTC date, or `SOURCE_DATE_EPOCH`, so a
scheduled site needs a rebuild (a cron job, say) on the day. `ssg build --drafts` and
`future = true` in `site.toml` build scheduled pages too.

As with `draft`, a `date` written directly in the frontmatter (without `{{`) keeps the page from
being rendered at all; a computed one is rendered first and then dropped.

## Data files

`.toml` and `.json` files directly inside `data/` (in the content dir) are read at the start of
a build and become template keys. They are not published; other files in `data/` are copied
like any asset. `data/site.toml` with `author = "Ann"` gives `{{data.site.author}}`:

```toml
# data/site.toml
author = "Ann"
tags = ["a", "b"]
[social]
mastodon = "@ann"
```

```json
{"items": [{"label": "Home", "url": "/"}], "count": 2, "draft": false, "note": null}
```

| source | key | value |
| --- | --- | --- |
| `site.toml` `author` | `data.site.author` | `Ann` |
| `site.toml` `[social] mastodon` | `data.site.social.mastodon` | `@ann` |
| `site.toml` `tags` | `data.site.tags.0`, `data.site.tags.1`, `data.site.tags.length` | `a`, `b`, `2` |
| `.json` `items[0].label` | `data.<file>.items.0.label` | `Home` |
| `.json` `count`, `draft`, `note` | `data.<file>.count` and so on | `2`, `false`, empty |

- Everything is text: booleans are `true`/`false`, numbers keep the digits as written (JSON) or
  their decimal form (TOML), `null` is empty (so `{{if data.x.note}}` is false). The `[...]`
  length key is how a template tells a list of three from a list of none, since templates have
  no loops; pick items by index.
- The parser for `.toml` is the one `site.toml` uses (see [TOML subset](#toml-subset)); JSON is
  standard JSON, up to 64 levels deep.
- The file name (letters, digits, `-`, `_`) is the first part of the key. If `site.toml` and
  `site.json` both exist, the one sorting later wins, with a warning. A key that is not a bare
  name (`"bad key"`) is skipped with a warning.
- Data has the lowest priority, below the built-in keys: a page can override `data.site.author`
  in its frontmatter. Values are text: `{{` inside one is not interpreted.
- A syntax error stops the build, naming the file and line.

## Pagination

`paginate: "10"` in the frontmatter of an index page (`index.md`, the one with `{{children}}`)
shows ten children per page. The index page is page 1; the rest are generated at
`<dir>/page/2/`, `<dir>/page/3/` and so on, from the same source (same frontmatter, same
template). Each page's `{{children}}` is its own slice, in the usual order (`order`, then `date`
ascending, then file name); put `order` on the children, or write the index page's list yourself,
for newest first.

| key | meaning |
| --- | --- |
| `page_number` | this page's number, from 1 |
| `page_count` | how many pages the section makes (1 when it fits on one) |
| `prev_page_url` | URL of the previous page; empty on page 1 |
| `next_page_url` | URL of the next page; empty on the last |

```markdown
---
title: "Posts{{if prev_page_url}}, page {{page_number}}{{end}}"
paginate: "10"
---
{{children}}

{{if prev_page_url}}[Newer]({{prev_page_url}}){{end}}
{{if next_page_url}}[Older]({{next_page_url}}){{end}}
```

- The keys are defined on every page of a paginated section, with empty values at the ends,
  so `{{if prev_page_url}}` is the test. They are not defined elsewhere.
- Only the index page's own `{{children}}` is cut; the children keep seeing each other as
  `prev_*` / `next_*` siblings and `parent_url` is still the index page. The extra pages are
  not anybody's child or sibling, but do appear in the sitemap and the manifest (their
  `source` is the virtual `<dir>/page/2/index.md`).
- A value that is not a whole number from 1 to 100000, or `paginate` on a page that is not an
  `index.md`, is ignored with a warning. A page already at `<dir>/page/K/` wins, with a warning.

## Redirects

`redirects.txt` in the content dir (never published) lists one rule per line: `from to [status]`.
Blank lines and `#` comments (also at the end of a line) are ignored.

```
# old address     new address              status (default 301)
/old              /new/
/blog/*           /posts/:splat            302
/gone/            https://example.com/x    308
```

- `from` must start with `/`; `to` is a path starting with `/` or an `http(s)://` URL. The status
  is 301, 302, 303, 307 or 308. `*` and `:name` are passed through for the host to interpret.
- A bad line, or a second rule for the same `from`, is skipped with
  `ssg: warning: redirects.txt: line N: ...`; the others still apply.
- The rules are written to `dist/_redirects` as `from to status` lines, the format Cloudflare
  Pages and Netlify read, and listed in the [manifest](#manifestjson) as `redirects[]` for the
  adapters that need another format. A hand-written `_redirects` in the content dir wins, with
  a warning.
- With `redirect_pages = true`, each rule without a wildcard also gets an HTML page with
  `<meta http-equiv="refresh">`, a `noindex` hint and (when `base_url` is set, or the target is a
  URL) a canonical link, for hosts that ignore `_redirects`. `/old` and `/old/` become
  `old/index.html`; `/old.html` becomes `old.html`. A page or file already at that path wins, with
  a warning.

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
- `404` and pages with a truthy `noindex` are left out too.
- A page whose final `sitemap` value is falsy (`false`, `0`, `no`, `off`, or empty) is left out.
  The value may come from a template.
- Drafts are listed only with `--drafts`.
- If the content root has its own `sitemap.xml` (not ignored), it is copied instead and the build
  warns: `ssg: warning: sitemap.xml exists in the content dir; not generating one`.

`base_url` should be the origin the site is served from at `/`. A path in it
(`https://x.dev/blog`) is prefixed to sitemap entries only; `{{url}}` and the links ssg generates
stay root-relative.

## manifest.json

Every `ssg build` writes a manifest describing what it published. Header files, deploy adapters
and plugins read it, so nothing has to guess. It is written **beside dist** (`dist/../manifest.json`),
never inside it, so it is not deployed. Set `manifest = "path"` in `site.toml` to move it, or
`manifest = false` to skip it. With `--output DIR` it goes beside DIR instead (a configured path
belongs to the configured dist and is not used). `check`, `list`, `serve` and `watch` write none.

```json
{
  "schema": 1,
  "ssg_version": "1.0.0",
  "build_date": "2026-03-04",
  "base_url": "https://example.com",
  "pages": [
    {"url": "/a/", "source": "a.md", "title": "A", "date": "2026-03-05",
     "outputs": [{"path": "a/index.html", "format": "html", "content_type": "text/html; charset=utf-8"}, ...]}
  ],
  "assets": [{"path": "img/x.png", "output": "img/x.png", "size": 5},
             {"path": "style.css", "output": "style.css", "size": 34,
              "fingerprint": "style.1a2b3c4d5e.css", "fingerprint_size": 34, "immutable": true}],
  "inline_styles": [{"path": "critical.css", "size": 120, "csp_hash": "sha256-..."}],
  "redirects": [{"from": "/old", "to": "/new/", "status": 301}],
  "plugins": [{"name": "search", "version": "1.0.0", "protocol": 1}]
}
```

- `schema` is an integer; fields are only added within a schema. Readers must ignore fields they
  do not know.
- `build_date` honours `SOURCE_DATE_EPOCH`, so reproducible builds give identical manifests.
- Each page lists its `url`, `source` (relative to the content dir), `title` (the same value
  used for navigation links: `nav_title`, `page_title`, `title`, then the file name), `date`
  (as written, `""` if none) and three `outputs` with `path` (relative to dist), `format`
  (`html`, `md`, `txt`) and `content_type`. Pages are in walk order; drafts that were not built
  are absent.
- Each asset has `path` (its source), `output` (where it is published) and `size` in bytes. An
  asset used through `{{asset}}` also has `fingerprint` (the hashed name, published next to the
  plain copy), `fingerprint_size` and `immutable: true`: the hashed file never changes, so it may
  be cached forever.
- `inline_styles` lists each stylesheet embedded with `{{inline}}`: `path`, `size` and
  `csp_hash`, the `sha256-...` source for a `style-src` header. It is the hash of the exact
  text between the `<style>` tags, which is right when the tag is in the skeleton.
- `redirects` lists the rules of `redirects.txt` in file order, as `from`, `to` and `status`
  (an integer). `plugins` lists each plugin that ran, in order: `name`, `version` and `protocol` (empty with
  `--no-plugins`).
- If the target exists and ssg did not write it (for instance a web app `manifest.json` in the
  site root), ssg warns and leaves it alone. A manifest it wrote is never copied into dist as
  an asset. A path inside dist is an error.
- `build --json` reports the path as `manifest` (`null` if none was written).
