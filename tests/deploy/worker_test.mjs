// usage: node worker_test.mjs <worker.js> <dist>
// Runs the generated Cloudflare worker against a built site, with a stand-in for the ASSETS
// binding (exact files only, like html_handling = "none"). Prints "ok" or the failures.
import { readFile, copyFile } from "node:fs/promises";
import { existsSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { pathToFileURL } from "node:url";

const [workerPath, dist] = process.argv.slice(2);
const copy = join(tmpdir(), `ssg-worker-${process.pid}.mjs`);
await copyFile(workerPath, copy);
const worker = (await import(pathToFileURL(copy).href)).default;

const env = {
  ASSETS: {
    async fetch(request) {
      const path = decodeURIComponent(new URL(request.url).pathname);
      const file = resolve(join(dist, path));
      if (path.endsWith("/") || !existsSync(file) || !statSync(file).isFile())
        return new Response("not found", { status: 404 });
      return new Response(await readFile(file), { status: 200 });
    }
  }
};

let failures = 0;
const fail = (msg) => { failures++; console.log("FAIL " + msg); };

async function get(path, accept) {
  const headers = accept === undefined ? {} : { Accept: accept };
  const res = await worker.fetch(new Request("https://example.com" + path, { headers, redirect: "manual" }), env);
  return { res, body: res.status < 300 || res.status === 404 ? await res.text() : "" };
}

function expect(name, got, want) {
  if (got !== want) fail(`${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
}

const html = "text/html,application/xhtml+xml;q=0.9,*/*;q=0.8";
const cases = [
  // path, accept, status, format, body starts with, content type
  ["/", html, 200, "html", "<", "text/html; charset=utf-8"],
  ["/", "text/markdown", 200, "md", "# Home", "text/markdown; charset=utf-8"],
  ["/", "text/plain", 200, "txt", "Home", "text/plain; charset=utf-8"],
  ["/", "*/*", 200, "txt", "Home", "text/plain; charset=utf-8"],
  ["/", undefined, 200, "txt", "Home", "text/plain; charset=utf-8"],
  ["/about/", "text/markdown, text/html", 200, "html", "<", "text/html; charset=utf-8"],
  ["/pricing/", "text/markdown", 200, "md", "# Pricing", "text/markdown; charset=utf-8"],
  ["/nope/", html, 404, "html", "<", "text/html; charset=utf-8"],
  ["/nope/", "text/markdown", 404, "md", "# Not found", "text/markdown; charset=utf-8"],
  ["/nope", "text/plain", 404, "txt", "Not found", "text/plain; charset=utf-8"],
  ["/404/", "text/markdown", 404, "md", "# Not found", "text/markdown; charset=utf-8"],
  ["/404", "text/markdown", 404, "md", "# Not found", "text/markdown; charset=utf-8"],
  ["/.ssg-dist", html, 404, "html", "<", "text/html; charset=utf-8"],
];
for (const [path, accept, status, format, start, type] of cases) {
  const name = `${path} [${accept}]`;
  const { res, body } = await get(path, accept);
  expect(name + " status", res.status, status);
  expect(name + " X-SSG-Format", res.headers.get("X-SSG-Format"), format);
  expect(name + " Content-Type", res.headers.get("Content-Type"), type);
  expect(name + " Vary", res.headers.get("Vary"), "Accept");
  if (!body.startsWith(start)) fail(`${name}: body starts ${JSON.stringify(body.slice(0, 20))}`);
}

// Link only on HTML pages; no Cache-Control on a 404.
let r = await get("/about/", html);
expect("Link", r.res.headers.get("Link"), '</about/index.md>; rel="alternate"; type="text/markdown"');
expect("Cache-Control", r.res.headers.get("Cache-Control"), "public, max-age=300");
r = await get("/about/", "text/markdown");
expect("Link on md", r.res.headers.get("Link"), null);
r = await get("/nope/", html);
expect("Link on 404", r.res.headers.get("Link"), null);
expect("Cache-Control on 404", r.res.headers.get("Cache-Control"), null);

// /about is a directory page: redirect to the slash form.
r = await get("/about", html);
expect("/about status", r.res.status, 301);
expect("/about location", r.res.headers.get("Location"), "https://example.com/about/");

// Real files pass through untouched.
r = await get("/style.css", "text/markdown");
expect("/style.css status", r.res.status, 200);
expect("/style.css Vary", r.res.headers.get("Vary"), null);

console.log(failures ? `${failures} failure(s)` : "ok");
process.exit(failures ? 1 : 0);
