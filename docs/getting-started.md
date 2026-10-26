# Getting started

From an empty directory to a deployed site: one page, a layout template, a three-part guide with
prev/next links and a table of contents, an `.assetignore`, a local preview, and deployment to
Cloudflare Workers or a plain static host.

Every file below is complete; the outputs shown are what ssg 0.2.0 produces.

- [1. Install](#1-install)
- [2. A first page](#2-a-first-page)
- [3. A layout template](#3-a-layout-template)
- [4. Assets](#4-assets)
- [5. A series](#5-a-series)
- [6. A table of contents](#6-a-table-of-contents)
- [7. Drafts](#7-drafts)
- [8. Keeping files out: .assetignore](#8-keeping-files-out-assetignore)
- [9. Building](#9-building)
- [10. Previewing](#10-previewing)
- [11. Deploying](#11-deploying)

## 1. Install

```
git clone https://github.com/isworking/ssg && cd ssg
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~/.local   # ~/.local/bin/ssg plus the man pages
ssg --version                             # ssg 0.2.0
```

Or skip the install and run `build/ssg` directly.

## 2. A first page

A site is a directory with a `site.toml` in it. Three keys are required:

```
mysite/
  site.toml
  skeleton.html
  index.md
```

`site.toml`:

```toml
dist = "dist"                  # output dir
templates = "templates"        # where {{extend}} and {{include}} look
skeleton = "skeleton.html"     # the HTML shell around every page
```

Paths are relative to the content dir. The templates dir doesn't have to exist until a page
uses it. All keys: [configuration.md](configuration.md).

`skeleton.html` is the HTML around every page. ssg puts the page's `title` into `<title>` and the
rendered page into `<main>`:

```html
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
```

`index.md`:

```markdown
---
title: "Hello"
---
# Hello

First page.
```

The block between the `---` lines is frontmatter: `key: value` lines that become template keys.
Values may be `"double-quoted"` (verbatim, no escapes), `'single-quoted'`, or bare; a line
starting with `#`, or ` # ...` after a value, is a comment. See [templates.md](templates.md).

Build:

```
$ ssg mysite
ssg: built 1 page, copied 0 files in 5 ms
```

```
mysite/dist/.ssg-dist
mysite/dist/index.html
mysite/dist/index.md
mysite/dist/index.txt
```

`index.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Hello</title>
</head>
<body>
<main>
<h1 id="hello">Hello</h1>
<p>First page.</p>
</main>
</body>
</html>
```

`index.md` is the page's markdown with the frontmatter removed and templates resolved.
`index.txt` is plain text, wrapped at 80 columns:

```
Hello
=====

First page.
```

`.ssg-dist` marks the directory as ssg's: from now on ssg empties it before each build, so
deleted pages don't linger. See [9. Building](#9-building).

### Where pages go

Every `.md` file is a page. The output path decides the URL:

| source | output | URL |
| --- | --- | --- |
| `index.md` | `index.{html,md,txt}` | `/` |
| `about.md` | `about/index.{html,md,txt}` | `/about/` |
| `guide/index.md` | `guide/index.{html,md,txt}` | `/guide/` |
| `guide/install.md` | `guide/install/index.{html,md,txt}` | `/guide/install/` |
| `foo.md` next to `foo/index.md` | `foo.{html,md,txt}` | `/foo` |

The last row is the exception: when `foo/index.md` exists, `foo/` is taken, so `foo.md` keeps its
name. Most static hosts need configuration to serve `/foo` from `foo.html`; avoid the layout if
yours can't be configured.

## 3. A layout template

Templates are markdown files in the templates dir. A page wraps itself in one with
`{{extend "name"}}` as the first thing in its body; the page's body becomes the template's
`{{content}}`.

`templates/base.md`:

```markdown
---
title: "{{page_title}} | My site"
description: "My site."
---
# {{page_title}}

{{content}}
```

`index.md`:

```markdown
---
page_title: "Home"
---
{{extend "base"}}

Welcome. Read the [guide](guide/).
```

Keys come from the page first, then its templates (inner before outer), then the built-ins.
So `title` here is the template's `"{{page_title}} | My site"`, filled from the page:
`<title>Home | My site</title>`. A page can override `title` or `description` by setting
them itself. A key that is set nowhere stays as written: `{{nope}}` comes out as `{{nope}}`.

The skeleton can use keys too. They are HTML-escaped there, so they're safe in attributes.
Add a description and a stylesheet to `skeleton.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title></title>
  <meta name="description" content="{{description}}">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
<main>
</main>
<footer>Built {{build_date}}.</footer>
</body>
</html>
```

`build_date` is the build's UTC date, `YYYY-MM-DD`. All template syntax and built-in keys:
[templates.md](templates.md).

## 4. Assets

Every file that isn't `.md` is copied to dist at the same path:

```
echo 'body { max-width: 40em; margin: auto; }' > mysite/style.css
```

gives `dist/style.css`. Hidden files and directories (`.git`, `.env`, `.DS_Store`, ...) are never
published, except `.well-known/` directories (`security.txt`, ACME challenges). Neither are
`site.toml`, the skeleton, the templates dir, dist, or `.assetignore` files.

## 5. A series

A directory with an `index.md` is a section: its `index.md` lists the pages under it with
`{{children}}`, and each of those pages gets links to its parent and its neighbours.

```
mysite/
  guide/
    index.md
    install.md
    usage.md
  templates/
    base.md
    post.md
    partials/back.md
```

`guide/index.md`:

```markdown
---
page_title: "Guide"
description: "A three-part guide."
---
{{extend "base"}}

{{children}}
```

`guide/install.md`:

```markdown
---
page_title: "Installing"
order: "1"
---
{{extend "post"}}

A C++17 compiler and CMake.
```

`guide/usage.md`:

```markdown
---
page_title: "Using it"
order: "2"
---
{{extend "post"}}

Point it at a directory.
```

The post template extends `base` (extends nest) and adds the navigation.

`templates/post.md`:

```markdown
{{extend "base"}}

{{content}}

---

{{if prev_url}}
Previous: [{{prev_title}}]({{prev_url}})
{{end}}

{{if next_url}}
Next: [{{next_title}}]({{next_url}})
{{end}}

{{include "partials/back"}}
```

`templates/partials/back.md`:

```markdown
Back to [{{parent_title}}]({{parent_url}}).
```

- `{{include "partials/back"}}` pastes `templates/partials/back.md` (`.md` is added), minus one
  trailing newline.
- `prev_url` and friends are set only when that neighbour exists; unset, `{{prev_url}}` would be
  printed as written. Guard them with `{{if}}`.
- A `{{if}}`, `{{else}}` or `{{end}}` alone on its line is removed with its line, so the blocks
  above leave no stray text. The blank lines around them keep each link in its own paragraph.

Build, and `guide/index.html` contains:

```html
<h1 id="guide">Guide</h1>
<ul>
<li><a href="/guide/install/">Installing</a></li>
<li><a href="/guide/usage/">Using it</a></li>
</ul>
```

and `guide/usage/index.html`:

```html
<h1 id="using-it">Using it</h1>
<p>Point it at a directory.</p>
<hr />
<p>Previous: <a href="/guide/install/">Installing</a></p>
<p>Back to <a href="/guide/">Guide</a>.</p>
```

The rules:

- **Children** of `d/index.md` are `d/*.md` and `d/*/index.md`. A page's parent is the
  `index.md` of its directory (for `d/index.md`, the one above). Pages without one have no
  `parent_*`, `prev_*` or `next_*`.
- **Order**: numeric `order` first (pages without one go last), then `date` compared as a string
  (so use `YYYY-MM-DD`; undated last), then the source path.
- **Link titles** (in `{{children}}`, `prev_title`, ...) are the first non-blank of `nav_title`,
  `page_title`, `title`; else the file name (`install`), or the directory name for an `index.md`.
  Set `nav_title` when the sidebar label should be shorter than the page title.
- `{{children}}` is a markdown list, one `- [title](url)` per child, nothing when there are none.

## 6. A table of contents

`{{toc}}` is a nested list of links to the page's `##` and deeper headings. Add it to
`guide/install.md`:

```markdown
---
page_title: "Installing"
order: "1"
---
{{extend "post"}}

{{toc}}

## Requirements

A C++17 compiler.

## Building

Run cmake.

### Release builds

Add `-DCMAKE_BUILD_TYPE=Release`.
```

Output:

```html
<h1 id="installing">Installing</h1>
<ul>
<li><a href="#requirements">Requirements</a></li>
<li><a href="#building">Building</a>
<ul>
<li><a href="#release-builds">Release builds</a></li>
</ul>
</li>
</ul>
<h2 id="requirements">Requirements</h2>
```

- The toc always goes on lines of its own, so put `{{toc}}` on its own line, not inside a
  paragraph, list or quote.
- `#` headings (the page title) aren't listed. A page with no `##`+ headings gets an empty toc,
  and `{{if toc}}` is false there, so a template can do:

  ```markdown
  {{if toc}}
  **Contents**

  {{toc}}
  {{end}}
  ```

- In the skeleton, `{{toc}}` inserts the toc as HTML, e.g. for a sidebar outside `<main>`.
- Links need heading ids, which are on by default (`heading_ids = true`); `## Title {#custom}`
  sets one by hand.

## 7. Drafts

A page whose `draft` is truthy is left out entirely: not built, not in `{{children}}`, not a
neighbour, not in the sitemap.

```markdown
---
page_title: "FAQ"
order: "3"
draft: "true"
---
```

Truthy means set, non-blank, and not `false`, `0`, `no` or `off` (any case). `ssg --drafts
mysite` builds drafts as normal pages.

## 8. Keeping files out: .assetignore

Files that live in the content dir but aren't content (deploy config, notes) go in an
`.assetignore`, with gitignore syntax:

```gitignore
# Deployment config, not site content.
worker.js
wrangler.jsonc
# Working notes.
notes/
```

- An `.assetignore` can sit in any directory; its patterns are relative to that directory, and
  deeper files win over shallower ones. `!pattern` re-includes.
- `ignore = ["*.psd"]` in `site.toml` adds patterns at the lowest priority.
- Ignored `.md` files aren't built; ignored directories aren't read at all.
- A subdirectory that is its own git checkout (has a `.git`) is skipped without being listed,
  with a note:

  ```
  ssg: note: skipping builder/ (a nested git checkout; set nested_repos = true to build it)
  ```

  Listing it in `.assetignore` skips it silently.

Exact matching rules: [configuration.md](configuration.md).

## 9. Building

```
$ ssg mysite
ssg: built 4 pages, copied 1 file in 2 ms
```

| flag | |
| --- | --- |
| `--drafts` | build drafts too |
| `--check-links` | warn about local `href`/`src` targets that don't exist in dist |
| `--quiet`, `-q` | no summary line and no notes; warnings still print |

`--check-links` checks links that have no scheme, relative (`../x/`) or rooted (`/x/`), against
dist. A target exists if it is a file, a directory with `index.html`, or `target.html`:

```
$ ssg --check-links mysite
ssg: warning: guide/usage.md: broken link ../nope/
ssg: built 4 pages, copied 1 file in 4 ms (1 warning)
```

Warnings never change the exit status (0). Errors stop the build with status 1:

```
ssg: guide/usage.md: missing template "post" (looked for /home/me/mysite/templates/post.md)
ssg: guide/usage.md: frontmatter line 2: expected key: value, got: title x
ssg: site.toml:5: 'clean' must be a boolean
```

### The dist directory

- Before each build ssg empties dist, but only if dist is missing, empty, or has the `.ssg-dist`
  marker from an earlier build. Otherwise it warns and builds over what's there:

  ```
  ssg: warning: not cleaning /home/me/mysite/dist: it wasn't created by ssg (no .ssg-dist); delete it once by hand
  ```

  This happens once for a dist made by an older ssg: delete it, and the next build marks it.
- `clean = false` in `site.toml` never empties it.
- A dist that is the content dir or contains it, or contains the templates dir or skeleton, or
  sits in a hidden directory, is an error before anything is written.
- `.ssg-dist` is deployed along with the site. It's a one-line text file; see
  [11. Deploying](#11-deploying) to keep it out.

### Sitemap

With `base_url = "https://example.com"` in `site.toml`, ssg also writes `dist/sitemap.xml` with
every page; `<lastmod>` comes from a `date: "YYYY-MM-DD"` key. `sitemap: "false"` in a page's
frontmatter leaves it out.

### Reproducible builds

`build_date` is today's UTC date unless `SOURCE_DATE_EPOCH` (seconds since 1970) is set:

```
SOURCE_DATE_EPOCH=$(git -C mysite log -1 --format=%ct) ssg mysite
```

## 10. Previewing

dist is plain files. Any static server works:

```
python3 -m http.server -d mysite/dist 8000
```

- `http://localhost:8000/guide/` serves `guide/index.html`, and `/guide` redirects to `/guide/`.
- The other formats are at their file names: `/guide/index.md`, `/guide/index.txt`.
- `python3 -m http.server` does no content negotiation and doesn't map `/foo` to `foo.html`.

Rebuild after each change; there is no watch mode. One way, with
[entr](https://eradman.com/entrproject/) (leave dist out of the watched files, or every build
triggers the next):

```
find mysite -path mysite/dist -prune -o -type f -print | entr -c ssg mysite
```

Text output is easy to check without a browser:

```
cat mysite/dist/guide/usage/index.txt
```

```
Using it
========

Point it at a directory.

----------------------------------------

Previous: Installing </guide/install/>

Back to Guide </guide/>.
```

## 11. Deploying

Upload dist. What differs between hosts is whether a URL can serve the `.md` and `.txt` versions.

### Cloudflare Workers, with content negotiation

This is how [isworking.dev](https://isworking.dev) is served. Each URL answers in the format the
client asks for:

| `Accept` contains | `/guide/` serves |
| --- | --- |
| `text/html` (browsers) | `guide/index.html` |
| `text/markdown` | `guide/index.md` |
| anything else (`curl`, `*/*`) | `guide/index.txt` |

Requests for real files (`/style.css`, `/guide/index.md`) get the file. `/foo` serves
`foo.EXT` if it exists, else redirects (301) to `/foo/` when `foo/index.EXT` exists, so
relative links resolve. Everything else is a 404.

Next to `site.toml`:

```
mysite/
  site.toml
  worker.js
  wrangler.jsonc
  .assetignore     # lists worker.js, wrangler.jsonc, builder/
```

`worker.js`:

```js
export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    // Real files (style.css, foo/index.html, ...) are served as they are.
    if (!path.endsWith("/")) {
      const exact = await env.ASSETS.fetch(request);
      if (exact.status !== 404) return exact;
    }

    const accept = request.headers.get("Accept") ?? "";
    let ext = ".txt";
    if (accept.includes("text/html")) ext = ".html";
    else if (accept.includes("text/markdown")) ext = ".md";

    const fetchAsset = (pathname) => {
      const u = new URL(request.url);
      u.pathname = pathname;
      return env.ASSETS.fetch(new Request(u, request));
    };

    let response;
    if (path.endsWith("/")) {
      // foo.md and foo/index.md are published as foo/index.{html,md,txt}.
      response = await fetchAsset(`${path}index${ext}`);
    } else {
      // foo.md stays foo.{html,md,txt} when foo/index.md also exists.
      response = await fetchAsset(`${path}${ext}`);
      if (response.status === 404) {
        // /foo is a directory page: redirect to /foo/ so its relative links resolve.
        const index = await fetchAsset(`${path}/index${ext}`);
        if (index.status !== 404) {
          url.pathname = `${path}/`;
          return Response.redirect(url.toString(), 301);
        }
      }
    }

    const headers = new Headers(response.headers);
    headers.set("Vary", "Accept");
    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers,
    });
  },
};
```

`wrangler.jsonc`:

```jsonc
{
  "$schema": "https://unpkg.com/wrangler@latest/config-schema.json",
  "name": "mysite",
  "main": "./worker.js",
  "compatibility_date": "2026-08-23",
  "assets": {
    "directory": "./dist",
    "binding": "ASSETS",
    // The worker sees every request, and asset lookups are exact.
    "run_worker_first": true,
    "html_handling": "none"
  }
}
```

`run_worker_first` sends every request through the worker. `html_handling: "none"` makes
`env.ASSETS.fetch` exact; the default would map `/foo` to `foo.html` and add or strip trailing
slashes itself, ahead of the worker's own rules.

Deploy by hand:

```
ssg mysite && (cd mysite && npx wrangler deploy)
```

Check:

```
curl https://example.com/guide/                              # text
curl -H 'Accept: text/markdown' https://example.com/guide/   # markdown
curl -sI -H 'Accept: text/html' https://example.com/guide    # 301 to /guide/
```

To keep `.ssg-dist` off the site, add `echo .ssg-dist > mysite/dist/.assetsignore` after the
build (Wrangler's own ignore file in the assets directory; note the spelling).

#### From GitHub Actions

`.github/workflows/deploy.yaml` in the content repo. It checks ssg out into `./builder` inside the
content dir, which ssg skips as a nested git checkout (and `.assetignore` lists it anyway):

```yaml
name: Deploy

on:
  push:
    branches: [main]

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/checkout@v4
        with:
          repository: isworking/ssg
          path: builder

      - name: Build ssg
        run: |
          cmake -S builder -B builder/build -DCMAKE_BUILD_TYPE=Release
          cmake --build builder/build

      - name: Build site
        run: ./builder/build/ssg .

      - uses: cloudflare/wrangler-action@v4
        with:
          apiToken: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          accountId: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          command: deploy
```

The `${{ ... }}` there is GitHub Actions syntax, not ssg's. A fresh checkout has no dist, so
every CI build starts clean.

### nginx, with content negotiation

The same behaviour as the worker:

```nginx
# Accept -> extension. Regexes are tried in order, so text/html wins over text/markdown.
map $http_accept $ssg_ext {
    default           txt;
    ~text/html        html;
    ~text/markdown    md;
}

server {
    listen 80;
    server_name example.com;
    root /srv/example.com/dist;
    add_header Vary Accept;

    # /dir/ -> /dir/index.EXT
    location ~ /$ {
        try_files ${uri}index.$ssg_ext =404;
    }

    # /file as is, else /foo -> /foo.EXT, else a redirect to /foo/
    location / {
        try_files $uri $uri.$ssg_ext @dir;
    }

    location @dir {
        if (-f $request_filename/index.$ssg_ext) {
            return 301 $uri/$is_args$args;
        }
        return 404;
    }

    location = /.ssg-dist {
        return 404;
    }
}
```

### Plain static hosts

nginx, Caddy, GitHub Pages, Netlify, S3 and the like serve `dir/index.html` for `/dir/` out of
the box, so the HTML site works as uploaded, and the `.md` and `.txt` files are reachable at
their file names (`/guide/index.md`). Content negotiation always needs server config like the
above. Without it:

- nginx: `try_files $uri $uri.html $uri/ =404;` also serves `foo.html` at `/foo`, for the
  `foo.md` next to `foo/index.md` layout.
- Caddy: `file_server` serves `index.html` and redirects `/dir` to `/dir/` by default.
- GitHub Pages: deploy dist with Actions (`actions/upload-pages-artifact` with `path: dist`, then
  `actions/deploy-pages`), not by pushing it to a branch. A branch deploy runs Jekyll, which
  also processes the `.md` files; if you must use one, add an empty `dist/.nojekyll` after the
  build (ssg never publishes hidden files itself).

## Next

- [templates.md](templates.md): every template feature, built-in key, and the skeleton.
- [configuration.md](configuration.md): `site.toml`, publishing and ignore rules.
- [markdown.md](markdown.md): the markdown dialect and the `.txt` format.
- [cli.md](cli.md): flags, `ssg render`, messages and exit codes.
- [`example/`](../example/): a complete site.
