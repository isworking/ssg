# Headers and deploy adapters

Every page is published as `index.html`, `index.md` and `index.txt`. A static host only serves the
right one for a request if something tells it how. `ssg build --target <name>` (or `[deploy]
target` in `site.toml`) writes that something for Cloudflare Workers, Vercel or nginx, from what
the build just published. Settings: [`[deploy]` and `[headers]`](configuration.md#deploy-and-headers).

Choosing the format is the same on every target: `text/html` in `Accept` gets HTML, else
`text/markdown` gets Markdown, anything else (`curl`, `*/*`, no header) gets plain text.

| Target | Files written | Where |
| --- | --- | --- |
| `cloudflare` | `worker.js`, `wrangler.jsonc` | next to `site.toml` |
| | `_headers`, `.assetsignore` | in dist, every build |
| `vercel` | `vercel.json` | next to `site.toml` |
| `nginx` | `nginx.ssg.conf`, `nginx.ssg-maps.conf` | next to `site.toml` |

Files next to `site.toml` are yours once written: ssg never replaces one whose content differs
(it warns and keeps it) unless you pass `--force`. Files that match what ssg would write are left
alone silently. The adapter files are not published. The files in dist are regenerated each build;
a hand-written `_headers` in the content dir wins, with a warning.

## Headers

| Header | When |
| --- | --- |
| `Vary: Accept` | always, on every negotiated page |
| `Content-Type` | always: `text/html`, `text/markdown` or `text/plain`, each with `charset=utf-8` |
| `Link: </guide/index.md>; rel="alternate"; type="text/markdown"` | HTML pages; `[headers] link = false` turns it off |
| `X-SSG-Format: html`, `md` or `txt` | the variant served; `[headers] format = false` turns it off |
| `X-SSG-Version` | opt-in, `[headers] version = true` |
| `Cache-Control` | `public, max-age=31536000, immutable` for fingerprinted files; `[headers] page_cache` (default `public, max-age=300`) for pages |

Source paths and build dates are never sent.

## Cloudflare Workers

`worker.js` is the worker from [getting-started.md](getting-started.md#cloudflare-workers-with-content-negotiation),
extended: it also sets the headers above, serves the 404 page in the format that was asked for with
status 404 (`/404/` itself too), and answers 404 for `/.ssg-dist`. `wrangler.jsonc` points the
assets binding at dist with `run_worker_first`. `_headers` marks each fingerprinted file immutable
(and adds `X-SSG-Version` if enabled). `.assetsignore` goes in dist, because that is where Wrangler
reads it, and keeps `.ssg-dist` off the site. Cloudflare allows 100 rules in `_headers`; a site
with more fingerprinted files than that needs the rules trimmed by hand.

## Vercel

`vercel.json` sets `outputDirectory`, `trailingSlash: true` and three `routes`, one per format,
that match the `Accept` header (`has` and `missing` conditions on a request header) and rewrite
`/guide/` to `/guide/index.md` or `.txt`. They are `routes` rather than `rewrites` because Vercel
gives static files precedence over rewrites, which would always serve the HTML. A `headers` rule
marks fingerprinted files immutable. A route also hides `/.ssg-dist`.

Checked against the Vercel project-configuration documentation (August 2026): header conditions
and per-route `headers` are documented. Not covered: a 404 page in the requested format (Vercel
cannot set a status from a rewrite, so Vercel's own 404 is used), `Link` headers (no per-page
header rule that does not need a rule per page), and `redirects.txt` (Vercel does not read
`_redirects`). Not yet verified on a live deployment.

## nginx

Include `nginx.ssg-maps.conf` in the `http` block and `nginx.ssg.conf` in the `server` block that
has `root` pointing at dist. The maps turn `Accept` into `$ssg_ext`; the server file uses it in
`try_files`, redirects `/foo` to `/foo/` for directory pages, serves the 404 page in the requested
format, answers 404 for `/.ssg-dist`, and sets the headers. It needs a stock nginx with `mime.types`.
Not exercised against a running nginx in ssg's tests.

## ssg doctor

`ssg doctor` warns when `.ssg-dist` is in dist and nothing excludes it. The `.assetsignore` that
`--target cloudflare` writes in dist counts, as does a `vercel.json` that names it.
