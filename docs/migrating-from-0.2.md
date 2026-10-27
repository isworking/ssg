# Migrating from 0.2 to 1.0

Every 0.2 invocation still works: `ssg <dir>`, `ssg render` and the flags keep their meaning, and
a site that needs none of the changes below builds to the same `dist`. The site behind
isworking.dev, a four-page Cloudflare Workers site, builds byte-identical `dist` output.

Work through this list, then build and look at the output (`ssg check` helps).

## Things that change what you publish

### 1. Raw HTML is allow-listed

Only ordinary content tags and attributes pass; the rest is escaped or dropped. Tags such as
`svg`, `form`, `button` and custom elements, and attributes such as `style`, now appear as text
or vanish.

Find them: build and search `dist` for `&lt;`. Keep them:

```toml
[markdown]
allow_tags  = ["my-card"]
allow_attrs = ["style"]
```

or set `scripts = true` to turn filtering off entirely (all pages must then be trusted).

### 2. Symlinks to files are skipped

A note is printed for each. Set `follow_symlinks = true` to get the 0.2 behaviour, or replace
the link with a copy. Symlinks to directories were never followed.

### 3. Scheduled pages are held back

A page whose `date` is after the build date is now treated like a draft. Build with `--drafts`,
or put `future = true` at the top of `site.toml`, to publish it anyway. Without a `date`, nothing
changes.

### 4. The sitemap leaves out some pages

`404` pages and pages with `noindex: "true"` or `sitemap: "false"` are no longer listed.

### 5. New generated files

These appear only if you have not written the file yourself (a hand-written file always wins):
`robots.txt` (allows everything and names the sitemap when `base_url` is set; disable or
configure it with `[robots]`). `feed.xml`, `_redirects`, taxonomy and pagination pages, hashed
CSS and the deploy files appear only when you add `[feed]`, `redirects.txt`, `[taxonomy.*]`,
`paginate`, `{{asset}}` or `--target`.

### 6. Reading time for CJK text

`reading_time` counts CJK characters at their own rate, so Chinese, Japanese and Korean pages
no longer get a time of one minute.

## Things that are new but change nothing until you opt in

- `manifest.json` is written **beside** `dist`, not inside it, so it is never deployed. If you
  deploy the parent directory, add it to your ignore file. A `manifest.json` that ssg wrote in
  `dist` in a pre-release is never published.
- Exit status 3 exists only with `--fail-on-warning`. Without it warnings still exit 0, so CI
  scripts do not break.
- Commands: `build`, `check`, `clean`, `doctor`, `init`, `new`, `serve`, `watch`, `completions`,
  `plugins`, `render`. `ssg <dir>` is a permanent alias of `ssg build <dir>`.
- `--json`, `--quiet`, `--incremental`, `--target`, `--output`; see [cli.md](cli.md).
- Plugins: add a `[plugin.<name>]` table. A `site.toml` that used a table by that name for
  something else would now be read as a plugin; no 0.2 key did.

## site.toml

The parser is now a TOML subset with dotted keys and nested tables. A 0.2 file parses the same;
a key that 0.2 silently ignored is still an "unknown key" warning. `[a.b]` before `[a]` warns once.

## Building ssg

No change to the dependencies (still none). New CMake options: `SSG_WERROR`, `SSG_SANITIZE`,
`SSG_FUZZ`, `SSG_PLUGINS` (on by default; builds the three first-party plugins). The version
comes from CMake; there is no second copy in the source.

## Checking your own site

```
ssg doctor site
ssg build --fail-on-warning site     # exit 3 on any warning
diff -r old-dist site/dist           # expect only the items above
```
