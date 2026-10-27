# Changelog

## 1.0.1 (2026-10-27)

- `--target` no longer warns on every build about adapter files you have edited (a renamed
  worker in `wrangler.jsonc`, a customised `worker.js`) or that an older ssg wrote. Files next to
  `site.toml` are written only when missing and then kept as they are; `--force` still
  regenerates them.

## 1.0.0 (2026-10-27)

The frozen surfaces are listed in [docs/stability.md](docs/stability.md); the upgrade steps in
[docs/migrating-from-0.2.md](docs/migrating-from-0.2.md). From here on, fixes only.

This changelog covers 0.2 to 1.0.0. Items marked (break) are described under Breaking changes.

### Breaking changes

Each of these changes what an existing site publishes. The migration steps are the shortest way
back to the old output.

#### Raw HTML is allow-listed, not deny-listed

With `html = true` and `scripts = false` (the defaults), raw HTML used to pass unless it used
one of about twenty known-dangerous tags. Now only a list of ordinary content tags and
attributes passes (see [docs/configuration.md](docs/configuration.md#security-html-and-scripts));
everything else is escaped (tags) or dropped (attributes). What changes in practice:

- `svg`, `math`, `form`, `input`, `button`, `canvas`, `label`, `select` and custom elements
  such as `<my-card>` now show as text.
- `style="..."` and other attributes outside the list (`align`, `width` on a `div`, `ping`,
  `formaction`, ...) are dropped.

Migration:

1. Build and look for escaped tags (`&lt;`) in `dist`.
2. Add the tags and attributes you rely on:
   `[markdown]` `allow_tags = ["my-card"]`, `allow_attrs = ["style"]`.
3. If every page is yours and you want no filtering at all, set `scripts = true`. That is
   unchanged, and is also what `ssg render --scripts` does.

#### Symlinks to files are skipped

A symlink to a file used to be copied (or rendered, for `.md`). A link in untrusted content
could therefore publish any file the build could read. They are now skipped with a note
(`ssg: note: skipping <path> (a symlink; ...)`, hidden by `--quiet`).

Migration: set `follow_symlinks = true` in `site.toml` if you rely on links, or replace the
link with a copy. Symlinks to directories were never followed and still are not.

### Other changes

#### Foundations

- Versioning is SemVer; the version lives in CMake only, and `ssg --version` prints it with the
  git commit when known. Release builds set `SSG_VERSION_SUFFIX`.
- `site.toml` is parsed by a shared TOML subset (nested tables, dotted keys, limits on nesting,
  control characters rejected). `[plugin.<name>]` tables are accepted.
- The build is a library (`ssg_core`) plus the binary; `SSG_WERROR`, `SSG_SANITIZE` and fuzz
  targets for the Markdown, template, TOML and ignore parsers.
- GitHub Actions: Linux (gcc, clang) and macOS, Debug and Release, sanitizers, the pinned
  CommonMark spec (652/652).

#### Command line

- Subcommands `build`, `check`, `clean`, `doctor`, `init`, `new`, `serve`, `watch`,
  `completions`, `plugins`, `render`; `ssg <dir>` still builds.
- Global `--json` (schema 1), `--quiet`, `--fail-on-warning` (exit 3), `--output`.
- `new` refuses absolute paths and `..`; `--output` never walks the configured dist.
- Warnings are recorded with their file.

#### Page model and manifest

- Each page is parsed once per render pass.
- `manifest.json` (schema 1) is written beside `dist` on every build: pages, outputs, content
  types, assets, CSP hashes.

#### Core features

- Scheduled pages (`future = true`), `404`/`noindex` out of the sitemap (break), CJK reading time.
- `canonical_url`, `absolute_url`, `og_title`, `og_description`; `data/*.toml` and `data/*.json`
  as `{{data.<file>.<key>}}`.
- `_redirects` from `redirects.txt`, a default `robots.txt` with `[robots]`, an Atom `feed.xml`
  from `[feed]`, taxonomy pages from `[taxonomy.<name>]`, `paginate: "N"`.
- `doctor` warns when `.ssg-dist` would be deployed.

#### CSS

- `{{asset "x.css"}}` (content-hashed copy), `{{inline "x.css"}}`, `[css] minify = true`
  (conservative minifier); SHA-256 for hashes.

#### Deploy

- `--target cloudflare|vercel|nginx` writes the Accept-routing files and headers; `[headers]` and
  `[deploy]` in `site.toml`; a 404 page in the example. The nginx output has a corrected
  `mime.types` nesting and fingerprint headers.

#### Plugins

- Protocol 1 (JSON lines): `--meta`, hooks for Markdown, `:::` containers, pages, assets and the
  finished build, per-page calls, plugin-added files, failure and timeout handling,
  `ssg plugins meta`. Frozen at this release.
- First-party plugins in `plugins/`: `highlight`, `images` and `search` (CMake option
  `SSG_PLUGINS`).

#### Serve, watch and incremental builds

- `serve` (live reload) and `watch`; `build --incremental` rewrites only changed files.
  Incremental output equals a clean build.

#### Hardening

- Allow-listed raw HTML (break); symlinks to files skipped (break).

