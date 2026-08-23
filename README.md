# ssg

Static site builder. One argument: the content directory.

```
cmake -B build && cmake --build build
./build/ssg path/to/content
```

## site.toml

Required, in the content dir:

```
dist = "dist"
templates = "templates"
skeleton = "skeleton.html"
```

Paths are relative to that directory.

## Pages

Markdown. Frontmatter is fenced with `---` and only `key: "value"` (quotes required). `{{name}}` in frontmatter or body is filled from those keys, or from a template; unknown keys stay as written.

`{{extend "base"}}` must be the first thing in the body (one per file). It loads `templates/base.md`. Nested extend works. The rest of the page is `{{content}}` in the template. Page keys override template keys.

The resolved `title` is written into the skeleton's `<title>`. HTML from the page goes in `<main>`. Each markdown file also writes `.md` and `.txt` next to the `.html`.

`foo.md` (not `index.md`) is published as `foo/index` unless `foo/index.md` already exists. The templates dir is not copied. Everything else is.

See `example/`.
