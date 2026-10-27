# Plugins

A plugin is a separate program that ssg starts once per build and talks to in JSON lines. It is
how you add syntax highlighting, image resizing, search, or anything else, without ssg growing a
scripting language. ssg has no in-process plugins and no sandbox.

> **Frozen.** Plugin protocol 1 is fixed from `1.0.0-rc.1`. Hooks and message fields are only
> ever added, and ssg refuses a plugin that asks for a newer protocol than it speaks. See
> [stability.md](stability.md).

- [Using plugins](#using-plugins)
- [The protocol](#the-protocol)
- [Hooks](#hooks)
- [Failure](#failure)
- [First-party plugins](#first-party-plugins)
- [Writing a plugin](#writing-a-plugin)

## Using plugins

Declare each one as a `[plugin.<name>]` table in `site.toml`. They run in the order the tables
appear. ssg looks for an executable called `ssg-plugin-<name>` on `PATH`, or at `path = "..."`
(relative to the site). It never looks in a `PATH` directory inside the content dir, so a site
can't ship a program that ssg then runs.

```toml
[plugin.highlight]

[plugin.images]
widths = [480, 960]
format = "webp"

[plugin.search]
output = "search-index.json"
required = false
```

| key | default | meaning |
| --- | --- | --- |
| `enabled` | `true` | `false` skips the plugin |
| `path` | | Use this executable instead of searching `PATH` |
| `required` | `true` | `false` turns that plugin's failures into warnings and skips it |
| `timeout` | `30` | Seconds each request may take (also for `--meta`) |

Every other key in the table is the plugin's own configuration. ssg checks it against the plugin's
`--meta`: a key the plugin doesn't declare is a warning, a value of the wrong type is an error, and
defaults are filled in before the plugin sees it.

Unless `--quiet`, a build lists every plugin it starts with its version and path. A plugin runs
with your full privileges: install only ones you trust. `ssg build --no-plugins` (or any command
with `--no-plugins`) runs none.

```
ssg plugins list         # each configured plugin: version and path, or why it isn't usable
ssg plugins meta search  # the plugin's --meta, as one line of JSON
ssg doctor               # the same checks, with the rest of the setup
```

## The protocol

**Self-description.** `ssg-plugin-<name> --meta` prints one JSON object and exits 0:

```json
{"name": "search", "version": "1.0.0", "protocol": 1, "pure": true,
 "description": "Writes a JSON search index",
 "hooks": ["init", "post_build"],
 "config": {"output": {"type": "string", "default": "search-index.json"}}}
```

`name`, `version` and `protocol` are required; `name` must match the table. `hooks` lists what the
plugin wants to be called for. `config` maps each key to its `type` (`string`, `bool`, `int`,
`array`, `object` or `any`) and optional `default`. `pure` says the output depends only on the
input.

**Running.** ssg starts the plugin with no arguments, with `SSG_PROTOCOL` (`1`) and
`SSG_BUILD_DATE` in the environment. It writes one JSON request per line to stdin and reads one
JSON reply per line from stdout. Anything on stderr is shown as a warning prefixed with the
plugin's name. When the build is done ssg closes stdin and the plugin should exit 0.

```
{"id": 7, "hook": "post_page", "params": {"page": "a.md", "url": "/a/", "html": "..."}}
{"id": 7, "result": "..."}
{"id": 7, "error": "what went wrong"}
```

The reply must carry the request's `id` and either `result` or `error`. Requests are answered in
order, one at a time.

## Hooks

A plugin only receives hooks it lists in `--meta`. `init` is sent first, only to plugins that list
it, and is the only way a plugin learns its config.

| hook | `params` | `result` |
| --- | --- | --- |
| `init` | `config` (the table, with defaults), `site`: `ssg_version`, `protocol`, `base_url`, `content_dir`, `dist_dir`, `build_date` | anything, e.g. `"ok"` |
| `transform_source` | `page` (source path), `url`, `markdown` | the Markdown, after templates are resolved and before it is parsed; it is also what `.md` publishes |
| `container:<name>` | `name`, `args` (the rest of the `:::` line), `body` (Markdown), `page` | Markdown that replaces the whole container |
| `post_page` | `page`, `url`, `html` | the page's HTML, after the skeleton; `.md` and `.txt` are untouched |
| `asset` | `path` (relative to the content dir), `source` (absolute), `out_dir` (a scratch directory) | a list of `{"path": "<where in dist>", "file": "<name under out_dir>"}`, empty for none |
| `post_build` | `manifest` (path to the build's manifest.json), `dist` | anything; it may add files under `dist` |

- `transform_source` and `post_page` are chained: each plugin gets the previous one's result.
- `container:<name>`: the first plugin that lists it handles `::: name args` ... `:::` (matched like
  the parser does, nested containers included, and not inside fenced code). Its Markdown goes
  through the normal pipeline and sanitizer, and is not scanned for containers again.
- `asset`: files travel by path, never as bytes in JSON. The original is still published; each
  returned file is added next to it, listed in the manifest, and usable with `{{asset "..."}}`. A
  `path` that is absolute or contains `..` is an error, and so is a `file` outside `out_dir`.
  `out_dir` is deleted when the build ends.
- `post_build`: with `manifest = false`, ssg still writes a manifest for the plugin to read, in a
  scratch directory. Files a plugin adds to `dist` are not in the manifest, so the generated headers and adapter files
  do not account for them. Per-page hooks (`transform_source`, `post_page`) make one call per page;
  batching is not part of protocol 1.

The manifest's `plugins` list records each plugin that ran: `name`, `version`, `protocol`.

## Failure

A non-zero exit, a reply that isn't JSON or has the wrong `id`, an `error` reply, a bad result or
a timeout fails the build, naming the plugin and hook:

```
ssg: plugin 'highlight', hook 'post_page': timed out after 30s
```

With `required = false` it is a warning instead, and the plugin is skipped for the rest of the
build. A plugin that can't be found, fails `--meta`, needs a newer protocol or is named differently
by `--meta` fails the same way. Plugins start before `dist` is touched, so a missing one leaves
the previous output alone.

## First-party plugins

Built from `plugins/` with the rest of ssg (`-DSSG_PLUGINS=OFF` skips them) and installed beside
`ssg`. They need no libraries.

**`ssg-plugin-highlight`** (`post_page`) wraps comments, strings, numbers and keywords in code
blocks in `<span class="hl-comment">`, `hl-string`, `hl-number` and `hl-keyword`. It knows c, cpp,
js, ts, go, rust, python, sh, toml, yaml, json and css; other blocks are left alone. The classes
are yours to style. Config: `prefix` (default `"hl-"`).

**`ssg-plugin-images`** (`asset`) writes resized copies of raster images next to the original:
`photos/cat.jpg` becomes `photos/cat-480.jpg`, `photos/cat-960.jpg`. Images are never scaled up.
It runs ImageMagick (`magick`, else `convert`) if installed, else macOS `sips`, which only writes
jpeg and png. With neither it warns and writes nothing. Config: `widths` (default `[480, 960]`),
`format` (`"same"`, or e.g. `"webp"`; needs ImageMagick), `quality` (80), `extensions`
(`["jpg", "jpeg", "png"]`).

**`ssg-plugin-search`** (`post_build`) writes `search-index.json` from the manifest and each
page's `.txt`: `{"version": 1, "pages": [{"url", "title", "date", "text"}]}`. Config: `output`
(a path inside dist), `max_chars` (5000 per page, 0 for no limit), `exclude` (URLs to leave out,
default `["/404/"]`).

## Writing a plugin

Any language works. A minimal one in shell, which has no JSON parser, can still answer `--meta` and
echo back a fixed result; real ones should parse each line. The first-party plugins use a small
shared loop in [`plugins/plugin_main.hpp`](../plugins/plugin_main.hpp) and ssg's own `json.hpp`.

Test by hand: `ssg-plugin-x --meta`, then
`echo '{"id":1,"hook":"post_page","params":{"html":"<p>hi</p>"}}' | ssg-plugin-x`.
