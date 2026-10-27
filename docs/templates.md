# Templates

Frontmatter, the template language, builtin keys and the HTML skeleton.

Related: [configuration](configuration.md) (`site.toml`), [markdown](markdown.md) (what the
page body becomes), [cli](cli.md) (`--drafts` and friends), [getting started](getting-started.md).

Template syntax in this file is shown as written. ssg would process it; GitHub doesn't.

- [Frontmatter](#frontmatter)
- [Template language](#template-language)
- [How a page is rendered](#how-a-page-is-rendered)
- [Builtin keys](#builtin-keys)
- [The skeleton](#the-skeleton)
- [Errors](#errors)
- [Cookbook](#cookbook)

## Frontmatter

### Fenced

The first line of the file is `---`; the next `---` line closes it. Surrounding blanks are
ignored on both fence lines. A UTF-8 BOM before the first fence is skipped.

```
---
title: "Proof that $a \neq b$"   # double: verbatim
quote: 'it''s'                   # single: '' is a quote
color: #fff                      # bare: '#' with no blank before it is kept
tags: one two # three            # bare: ' # ...' is a comment
empty:
# a comment line; blank lines are ignored too
---
Body.
```

| form | value |
| --- | --- |
| `"..."` | everything between the first and the **last** `"` on the line, verbatim. No escapes: `\n`, `\"`, `\\` stay as written. `"a "b" c"` is `a "b" c`. A `# comment` may follow the closing quote. |
| `'...'` | literal; `''` is one `'`. A `# comment` may follow. |
| bare | the rest of the line, trimmed. A `#` preceded by a blank starts a comment. `key:` alone is the empty string. |

- Keys are ASCII letters, digits, `_`, `-`, `.`, and are case sensitive. Blanks around the key
  and the `:` are ignored, so indented lines work.
- A repeated key: the last one wins.
- Values are templates themselves: `title: "{{series}}: Part 2"` works. Write `\{{` for a literal
  `{{` (see [escapes](#escapes)).
- Line endings: `\n`, `\r\n` or a lone `\r`.
- Any other line is an error: `frontmatter line N: expected key: value, got: <line>`, with the
  opening fence as line 1. No closing fence: `unclosed frontmatter (missing closing ---)`.

### The leading `---` caveat

A file whose first line is `---` always opens frontmatter. A page that starts with a thematic
break fails to build:

```
---

Some text
```

```
ssg: index.md: frontmatter line 3: expected key: value, got: Some text
```

Start it with an empty frontmatter block, or use `***` / `___`:

```
---
---

---

Some text
```

The same applies to partials and templates: they are parsed the same way.

A blank line before the first `---` means there is no frontmatter at all; the fence lines are then
markdown (a thematic break or a setext heading).

### Legacy unfenced form

Without a fence, the **leading run** of `key: "value"` lines is read as frontmatter and removed
from the body:

```
title: "Hello"
draft: "no"
# Heading {{title}}
```

Rules: double quotes only, the closing `"` must end the line (trailing blanks allowed), no
comments. The run stops at the first blank line or the first line that doesn't match. Nothing
after the first non-matching line is ever read as a var.

Pitfall: a page whose first line merely looks like a var loses it, silently.

```
Quote: "To be or not to be"

Body.
```

publishes only `Body.` (and defines a key `Quote`). Lines that don't end in `"`, such as
`Warning: "x" do this`, are left alone. Use fenced frontmatter for new pages; an empty
`---`/`---` block at the top also protects a page like the one above.

## Template language

Tags are `{{` ... `}}` on one line. Blanks (spaces, tabs) are allowed after `{{` and before `}}`,
and between words. Text in braces that isn't one of the tags below passes through unchanged:
`{{ a b }}`, `{{}}`, `{{a:1}}`, an unterminated `{{a`.

Templates are processed everywhere in a page: in prose, in inline code, in fenced code blocks
and in raw HTML. To show template syntax, escape it.

| tag | meaning |
| --- | --- |
| `{{key}}` | the key's value |
| `{{extend "name"}}` | wrap this page in `templates/name.md` (first thing in the body only) |
| `{{content}}` | in a template: the page (or inner template) being wrapped |
| `{{include "name"}}` | paste `templates/name.md` |
| `{{asset "path"}}` | the URL of a content-hashed copy of an asset, e.g. `/style.1a2b3c4d5e.css` |
| `{{inline "path.css"}}` | the contents of a stylesheet, for a `<style>` element |
| `{{if key}}` `{{if not key}}` `{{else}}` `{{end}}` | conditionals |
| `\{{` | a literal `{{` |

### Keys

`{{key}}` is replaced by the key's value, looked up in this order (first match wins):

1. the page's frontmatter;
2. the page's template, then the template that one extends, and so on outwards;
3. the [builtin keys](#builtin-keys).

So a page or template can override a builtin (`url: "/custom/"` changes `{{url}}`, not where the
page is written). Avoid builtin names for your own keys.

A value is itself expanded, recursively: `title: "{{page_title}} | site"` sees `page_title`
whichever file defines it.

Unknown keys stay as written, with the inner blanks removed: `{{ nope }}` comes out as
`{{nope}}`. This is not an error, so a typo shows up in the output, not in the build log.
(`base_url` from `site.toml` is not a key; `{{base_url}}` stays as written.)

Key names follow the frontmatter rules: `[A-Za-z0-9_.-]+`. `else` and `end` can't be used as
keys. `{{if}}` alone is a key named `if`; `{{if not}}` tests a key named `not`.

### `{{extend}}`

```
{{extend "base"}}
Page body.
```

- Must be the first thing in the body (leading blank lines and blanks are fine). Anywhere else it
  stays in the output as written.
- One line break right after the tag is dropped. Anything else on the same line (including
  trailing blanks) becomes the start of the content.
- `"name"`: double quotes only; `{{extend 'base'}}` is plain text. `.md` is added unless the
  name already ends in `.md`, so `"base"` and `"base.md"` are the same file. See
  [paths](#paths).
- The template's body is rendered with `{{content}}` set to the page body. A template can extend
  another; the chain may be any length. Extending a template already in the chain is an error
  (`cyclic {{extend}}`).
- Template frontmatter supplies defaults: a key the page (or an inner template) already defines
  is not changed.
- `content` is set by extend and overrides a frontmatter key of that name. A page without
  extend can define and use its own `content`.

There are no implicit layouts: a page without `{{extend}}` is rendered as is (plus the
skeleton). See [per-section templates](#per-section-templates).

### `{{include}}`

```
{{include "partials/nav"}}
```

- Pastes `templates/partials/nav.md`, evaluated in the including page's context: it sees every
  key the page sees.
- The partial may have frontmatter. Its keys are defaults for that partial only; any key the
  page, its templates or the builtins define wins over them.
- One trailing line break is removed from the partial, so `Hello {{include "name"}}!` stays on
  one line.
- Partials can include partials. A partial can't use `{{extend}}` (error).
- Includes are expanded in the final pass only, so a missing partial in an untaken `{{if}}`
  branch is not an error.
- Includes work in frontmatter values. The value must be single-quoted or use the
  first-to-last-quote rule; `\"` is not an escape:

  ```
  footer: '{{include "footer"}}'
  footer: "{{include "footer"}}"
  ```

- `.md` is appended unless the name ends in `.md`: `{{include "nav.html"}}` reads
  `nav.html.md`.

### `{{asset}}` and `{{inline}}`

```html
<link rel="stylesheet" href="{{asset "style.css"}}">
<style>{{inline "critical.css"}}</style>
```

Both work in the skeleton, templates and frontmatter values, and take a path relative to the
content dir (not the templates dir; paths may contain `/`). The file must be a published asset:
a missing file, a path with `..`, or a file that is ignored, hidden or a page is an error
naming the page.

- `{{asset "style.css"}}` also writes the file as `style.<hash>.css` (`<hash>` is the first 10 hex
  digits of the SHA-256 of the published bytes, so builds are reproducible) and expands to
  `/style.<hash>.css`. The plain `style.css` is still published. Any asset can be fingerprinted;
  `LICENSE` becomes `LICENSE.<hash>`. Only files named this way are fingerprinted, and `url(...)`
  inside a stylesheet is left alone.
- `{{inline "x.css"}}` expands to the stylesheet's text with trailing whitespace removed. It
  fails the build if the file contains `</style` (any case), and warns when it is larger than
  `css.inline_limit` bytes (default 16384, `0` turns the warning off). A `{{` inside the file
  stays literal. Inline in the skeleton: in a Markdown template the surrounding HTML block is
  subject to `html` and `scripts` handling.
- With `[css] minify = true` both use the minified file, so the hash and the inlined text match
  what is published.
- The manifest records each fingerprint and, for inlined files, the CSP source to put in
  `style-src` (see [manifest.json](configuration.md#manifestjson)).

Like `{{include}}`, both expand in the final pass only.

### Conditionals

```
{{if prev_url}}
Previous: [{{prev_title}}]({{prev_url}})
{{else}}
First post.
{{end}}
```

- `{{if key}}` is true when the key is defined and its **expanded** value, trimmed, is non-empty
  and not `false`, `0`, `no` or `off` (any case). `{{if not key}}` is the opposite.
- An undefined key is false. A key whose value is an unknown key, like
  `title: "{{page_title}}"` with no `page_title`, is true: its value is the literal text
  `{{page_title}}`.
- `{{else}}` is optional; one per `{{if}}`. They nest (up to 256 deep).
- The untaken branch is not evaluated: no includes, no errors from it.
- Conditionals are evaluated after the whole extend chain is merged, so a template can test page
  keys and a page can test template keys. They work in frontmatter values too:
  `title: "{{if kind}}[{{kind}}] {{end}}Hello"`.

Whitespace control: an `{{if}}`, `{{if not}}`, `{{else}}` or `{{end}}` tag alone on its line
(blanks around it allowed) removes the whole line, including its line break. A tag sharing a line
with anything else, even another tag, leaves the line. So

```
before
{{if x}}
yes
{{end}}
after
{{if nope}}inline{{end}}
last
```

gives, with `x` set and `nope` not:

```
before
yes
after

last
```

The skeleton does not do this; see [the skeleton](#the-skeleton).

### Escapes

`\{{` is a literal `{{` that is never read as a tag: `\{{title}}` prints `{{title}}`. It works in
the body, in templates, partials, frontmatter values and the skeleton, including inside code
blocks. Only `{{` needs escaping; `}}` alone is plain text.

`\\{{x}}` is `\` followed by the escape, i.e. `\{{x}}` in the generated `.md`, which markdown then
renders as `{{x}}` (its own `\{` escape).

Builtin values that come from file names or titles (`path`, `children`, `parent_title`,
`prev_title`, `next_title`) are escaped for you: a `{{` in a title stays literal.

### Paths

Names in `extend` and `include` are relative to the templates dir (`templates = ...` in
`site.toml`), never to the page. Forward slashes work for subdirectories. An absolute path or
any `..` component is an error, and so is an empty `{{include ""}}`. An empty `{{extend ""}}`
is dropped silently: the page is built with no template.

```
bad template path "../x" in <page> (must be relative to the templates dir, without "..")
```

### Cycles

- Keys: `a: "{{b}}"`, `b: "x {{a}}"` is not an error. Inside one expansion, a key that is
  already being expanded is left as written, so output ends in a literal `{{a}}` or `{{b}}`;
  because values are expanded more than once, part of the cycle may repeat
  (`{{a}}` above prints `x x x {{a}}`). Don't rely on the exact text.
- Includes: a partial that includes itself, directly or not, is an error.
- Extends: an error.

### Limits

| limit | error |
| --- | --- |
| 64 nested expansions (a key in a key's value, an include in an include; both count) | `template expansion nested more than 64 levels deep` |
| 256 nested `{{if}}` | `{{if}} nested too deeply` |
| 64 MiB from one evaluation | `template expansion larger than 64 MiB` |

Repeated keys and includes are memoised, so templates that reuse keys or partials many times
don't blow up in time.

## How a page is rendered

1. Frontmatter is parsed. If the body starts with `{{extend "x"}}`, the tag is removed.
2. For each template in the extend chain, innermost first: its keys are added where not already
   set, `content` is set to the body so far, and the template body is rendered. In this step only
   keys known so far are filled; conditionals, includes and other keys are kept for step 4.
3. Builtins are added for keys still unset.
4. Final pass: every key's value is evaluated (keys, conditionals, includes), then the body is
   evaluated against those values. `\{{` becomes `{{`.
5. Leading blank lines are dropped; the `{{toc}}` placeholder is expanded. This is the
   published `.md`. It is converted to HTML (placed in the skeleton) and to `.txt`.

The site is rendered twice. Pass 1 renders every page with the navigation builtins (`children`,
`parent_*`, `prev_*`, `next_*`, `toc`, `word_count`, `reading_time`) set to the empty string, to
learn each page's keys, link title, draft state, word count and whether it has `##` headings.
Pass 2 renders for real. So:

- a page's own `draft` must be decidable in pass 1 (it can't depend on `prev_url` etc.);
- in pass 2 `parent_*`, `prev_*`, `next_*` are **unset** when there is no such page, so
  `{{prev_url}}` outside an `{{if}}` prints as written;
- each template is evaluated several times per page. It has no side effects, so this only
  matters for build time.

An error in a template is reported against the page being built. Templates no page uses are
never read.

## Builtin keys

| key | value |
| --- | --- |
| `url` | the page's public path: `/`, `/blog/`, `/blog/post/` (or `/blog/post` for `post.md` beside `post/index.md`). Bytes outside `A-Za-z0-9-._~/` are percent-encoded: `café.md` is `/caf%C3%A9/`. |
| `path` | source path from the content dir, forward slashes: `blog/post.md` |
| `build_date` | UTC date of the build, `YYYY-MM-DD` |
| `word_count` | words in the page |
| `reading_time` | minutes at 200 words a minute, rounded up, at least `1` |
| `children` | markdown list of links to the pages under this index page; empty elsewhere |
| `parent_url`, `parent_title` | the index page above |
| `prev_url`, `prev_title`, `next_url`, `next_title` | the neighbouring siblings |
| `toc` | a nested list of links to the page's `##`..`######` headings |
| `absolute_url`, `canonical_url` | `base_url` + `url`, e.g. `https://example.com/blog/`. Only defined when `base_url` is set, so `{{if canonical_url}}` works. Set `canonical_url:` on a page to point it elsewhere. |
| `feed_url` | `base_url/feed.xml`; defined only when a `[feed]` is being written |
| `page_number`, `page_count`, `prev_page_url`, `next_page_url` | on the pages of a section using `paginate`; see [Pagination](configuration.md#pagination) |
| `taxonomy`, `taxonomy_title`, `term`, `term_slug`, `term_url`, `term_count`, `terms`, `pages`, `<name>_links` | taxonomy pages and the pages they group; see [taxonomies](configuration.md#taxonomyname) |
| `data.<file>.<key>` | values from `data/*.toml` and `data/*.json`; see [Data files](configuration.md#data-files) |
| `og_title` | the page's `title`, or the link title navigation would use (see below) when it has none |
| `og_description` | the page's `description`, or empty |

All are strings, and all can be overridden by the page or a template.

### Canonical and Open Graph keys

These exist so a skeleton can write its `<head>` without repeating itself:

```html
<head>
  <title></title>
  {{if canonical_url}}<link rel="canonical" href="{{canonical_url}}">{{end}}
  <meta property="og:title" content="{{og_title}}">
  {{if og_description}}<meta property="og:description" content="{{og_description}}">{{end}}
  {{if absolute_url}}<meta property="og:url" content="{{absolute_url}}">{{end}}
</head>
```

Values placed in the skeleton are attribute-escaped. All four are ordinary defaults, so a page's
own `og_title:` or `canonical_url:` wins. `og_title` and `og_description` are empty in the first
pass, like the navigation keys.

### `build_date`

The current UTC date. With `SOURCE_DATE_EPOCH` set (seconds since 1970, `0` to
`253402300799`), that time is used instead, for reproducible builds:

```
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) ssg .
```

Anything else (not an integer, negative, past 9999-12-31) is ignored with
`ssg: warning: ignoring SOURCE_DATE_EPOCH=<v> (want seconds from 0 to 253402300799); using the current time`.

### `word_count`, `reading_time`

Counted on the page's plain text (the `.txt` rendering, wrapped at `text_width`) from pass 1: the page after
its templates, with the navigation builtins empty. Template text such as a footer counts; the
skeleton doesn't. A word is a whitespace-separated run with at least one ASCII letter or digit or
any non-ASCII character, so `--` and `*` don't count, but `—` does. Han ideographs, kana and
the CJK extension blocks are written without spaces, so each such character counts as one word
of its own (Hangul is spelled with spaces and is counted like any other text). `reading_time`
assumes 200 words or 500 CJK characters a minute: `ceil((words + cjk * 0.4) / 200)`, minimum 1,
where `words` excludes the CJK characters. 401 Latin words is `3`; 501 Han characters is `2`.

### Parents, children, siblings

The parent of `d/x.md` is `d/index.md`; the parent of `d/index.md` is `index.md` in the
directory above. The root `index.md` is the parent of the top-level pages. A missing index page
means no parent: the pages below it have no `parent_*`, `prev_*` or `next_*`.

Children of `d/index.md`: `d/*.md` (except `index.md`) and `d/*/index.md`. Only index pages have
children; on any other page `children` is empty.

Caveat: `foo.md` next to a `foo/` directory without `foo/index.md` is published at `/foo/`, but
it is not an index page. `foo/bar.md` has no parent and `foo.md` has no children. Rename
`foo.md` to `foo/index.md` to get the navigation.

Siblings (a parent's children) are sorted by:

1. `order`, numerically (`1`, `-2`, `1.5`). Pages with an order come first. A value that isn't a
   number (or is `nan`) counts as no order.
2. `date`, as a **string**, ascending: oldest first, so `prev` is older. Dated pages come before
   undated ones. Use ISO dates (`2024-02-01`); `2024-2-1` sorts after `2024-10-01`.
3. source path, bytewise.

`prev_*`/`next_*` are the neighbours in that order; the first has no `prev`, the last no `next`.
Drafts (truthy `draft`, unless `--drafts`) are left out everywhere: they aren't built, listed or
linked.

`children` is one `- [title](url)` line per child, no trailing line break, title
markdown-escaped. Put it on a line of its own:

```
{{children}}
```

gives

```
- [First](/notes/first/)
- [The \*Blog\*](/blog/)
```

### Link titles

`children`, `parent_title`, `prev_title` and `next_title` use the linked page's first non-blank
`nav_title`, `page_title`, `title` (after its templates, so a template can set them), else the
file name without `.md`, or for an index page its directory name (`index` for the root). `0` and
`Off` are valid titles.

Titles may use navigation keys (`nav_title: "{{parent_title}}: {{note_title}}"`); they resolve
the same whatever order the files are read in.

`parent_title`, `prev_title`, `next_title` are inserted as written, not markdown-escaped (unlike
`children`): a title with `*` is emphasised in `[{{prev_title}}]({{prev_url}})`, and one with `]`
breaks the link.

### `toc`

On a page with `##`..`######` headings, `{{toc}}` becomes a nested markdown list of links to them:

```
- [One](#one)
  - [One.a](#onea)
- [Two code \& em](#two-code--em)
```

- `#` headings are left out. The shallowest heading level present is the top; a level can only
  go one deeper than the previous item (a `####` straight after a `##` is indented once).
- Links use the heading ids (`heading_ids = true`, or `{#custom}`). A heading without an id is
  listed as plain text.
- The list is built from the final page, so headings that come from templates are included.
- On a page without such headings `toc` is empty and `{{if toc}}` is false.

Placement: the list always goes on lines of its own, with blank lines around it, splitting the
line it was on. `Contents: {{toc}} more` becomes a paragraph, the list, another paragraph.
Inside a list item or blockquote it breaks that structure; put `{{toc}}` on its own line at the
top level. Through a frontmatter key, the toc is empty where the skeleton inserts that key, but
the full list where the page body or a template uses it; prefer `{{toc}}` directly. An
escaped `\{{toc}}` prints `{{toc}}`.

Caveat: a heading in the wrapper is listed too.

```
{{if toc}}
## Contents

{{toc}}
{{end}}
```

lists `Contents` as its first item. Use `**Contents**` instead, or put the toc in the
skeleton.

### Special frontmatter keys

| key | used for |
| --- | --- |
| `title` | the skeleton's `<title>`; link titles |
| `page_title`, `nav_title` | link titles |
| `order`, `date` | sibling order; `date` (`YYYY-MM-DD...`) is the sitemap `<lastmod>` |
| `draft` | truthy: not built unless `--drafts` |
| `sitemap` | falsy (`"false"`): left out of `sitemap.xml` |
| `content` | set by `{{extend}}` |

`draft` set in the page's own frontmatter (without template syntax) skips the page before it is
rendered, so a draft with a broken template doesn't fail the build. A `draft` set by a template,
or computed, takes effect after pass 1 rendering.

## The skeleton

The HTML shell every page goes into (`skeleton = ...` in `site.toml`). Example:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>isworking.dev</title>
  {{if description}}<meta name="description" content="{{description}}">{{end}}
  <link rel="canonical" href="https://isworking.dev{{url}}">
</head>
<body>
{{if toc}}<nav class="toc">{{toc}}</nav>{{end}}
<main></main>
</body>
</html>
```

- **Title.** If the page has a `title` key (after templates), the inside of the first `<title>`
  tag (any case, any attributes) is replaced by it, HTML-escaped. An empty `title` gives an empty
  `<title>`. Without a `title` key the skeleton's own text is kept, so it can hold a default.
- **Body.** The inside of the first `<main>` (any case, attributes kept) is replaced by the
  page HTML, on lines of its own. Whatever was there is dropped. If the skeleton contains
  `{{content}}`, that is replaced by the page HTML instead and `<main>` is optional and left
  untouched. Neither: `HTML skeleton has no <main tag`; `<main>` without `</main>`:
  `HTML skeleton has no </main> tag`.
- **`{{key}}`** is the page's value (with every builtin), HTML-escaped for text and either
  attribute quote (`& < > " '`). Values are not markdown-rendered: `{{children}}` prints
  markdown source.
- **`{{toc}}`** inserts the toc as HTML (`<ul>`...), empty when the page has none. `{{if toc}}`
  is true when it is non-empty. The toc is only computed if `{{toc}}` itself appears in the
  skeleton or the page, so a skeleton `{{if toc}}` with no `{{toc}}` anywhere is always false.
- **Conditionals** work as in templates, but tag lines are not removed: a false
  `{{if}}` on its own line leaves an empty line.
- **`\{{`** is a literal `{{`.
- Unknown keys stay as written. `{{include}}` and `{{extend}}` are not supported here and are
  printed as written.
- The skeleton's `{{if}}` structure is checked once at load; errors are prefixed with its path,
  e.g. `ssg: skeleton.html: missing {{end}} for {{if banner}}`.

Missing keys print as written, so guard optional ones with `{{if}}`: a page without
`description` would otherwise get `content="{{description}}"`.

## Errors

Errors print `ssg: <page>: <message>` and exit 1. Messages from template evaluation end in
`in <absolute path of the page>`, plus `(in include <absolute path of the partial>)` when raised
inside a partial. Errors in a template's body are reported against the page that uses it.

```
ssg: notes/one.md: missing {{end}} for {{if prev_url}} in /src/site/notes/one.md
```

| message | cause |
| --- | --- |
| `frontmatter line N: expected key: value, got: <line>` | a fenced frontmatter line that isn't a comment, blank or `key: value` (in a template or partial, prefixed with its path) |
| `unclosed frontmatter (missing closing ---)` | first line `---`, no closing fence |
| `missing template "x" (looked for <path>)` | `{{extend}}` or `{{include}}` file doesn't exist |
| `bad template path "x" in <page> (must be relative to the templates dir, without "..")` | empty, absolute or `..` name |
| `cyclic {{extend}} in <page>` | the extend chain repeats a template |
| `cyclic {{include "x"}} in <page>` | a partial includes itself, directly or not |
| `{{extend}} inside included template <path>` | an included partial starts with `{{extend}}` |
| `missing {{end}} for {{if x}}` | unclosed `{{if}}` |
| `{{end}} without {{if}}`, `{{else}} without {{if}}` | stray tag |
| `second {{else}} in one {{if}}` | two `{{else}}` |
| `{{if}} nested too deeply` | more than 256 nested `{{if}}` |
| `template expansion nested more than 64 levels deep` | key/include nesting |
| `template expansion larger than 64 MiB` | runaway output |
| `HTML skeleton has no <main tag`, `HTML skeleton has no </main> tag` | skeleton without `{{content}}` or a complete `<main>` |

Known quirk: a frontmatter key whose value includes a partial that uses that same key fails
with `cyclic {{include "x"}}` instead of leaving the key as written.

## Cookbook

All of these were built with the current binary. The layout:

```
site.toml
skeleton.html
templates/base.md
templates/section.md
templates/note.md
templates/partials/footer.md
notes/index.md
notes/one.md
notes/two.md
```

### Base template with a page footer

`templates/base.md`:

```
---
title: "{{page_title}} | isworking.dev"
---
# {{page_title}}

{{content}}

{{include "partials/footer"}}
```

`templates/partials/footer.md` (its own frontmatter, then a thematic break, which is why the
fences matter):

```
---
updated: "{{build_date}}"
---
---

{{reading_time}} min read · updated {{updated}} · [source](https://github.com/OWNER/REPO/blob/main/{{path}})
```

A page sets `updated: "2024-03-01"` to override the partial's default.

### Description meta tag and canonical link

In the skeleton:

```html
{{if description}}<meta name="description" content="{{description}}">{{end}}
<link rel="canonical" href="https://isworking.dev{{url}}">
```

`description` is escaped for the attribute. `base_url` isn't available as a key; hard-code the
origin, or set it once in `templates/base.md` (`origin: "https://isworking.dev"`) and use
`{{origin}}{{url}}` (only pages that extend `base` get it).

### Conditional banner

```html
{{if draft}}<p class="banner">Draft: not published.</p>{{end}}
```

Drafts are only built with `--drafts`, so the banner shows in preview builds only. The same
pattern works for any key: `{{if outdated}}...{{end}}` with `outdated: "yes"` on old pages.

### Per-section templates

There are no automatic per-directory layouts; each page names its template. A section index
lists its pages, and the pages get series navigation.

`templates/section.md`:

```
{{extend "base"}}
{{content}}

{{children}}
```

`templates/note.md`:

```
---
page_title: "{{series}}: {{note_title}}"
nav_title: "{{note_title}}"
---
{{extend "base"}}
{{if prev_url}}
Previous: [{{prev_title}}]({{prev_url}})
{{end}}

{{content}}

{{if next_url}}
Next: [{{next_title}}]({{next_url}})
{{else}}
Last in the series. Back to [{{parent_title}}]({{parent_url}}).
{{end}}
```

`notes/index.md`:

```
---
page_title: "Notes"
description: "Notes on things."
---
{{extend "section"}}
Everything I wrote down.
```

`notes/one.md`:

```
---
series: "Notes"
note_title: "One"
order: "1"
---
{{extend "note"}}
## Part

text
```

`notes/one/index.md` in dist:

```
# Notes: One


## Part

text


Next: [Two](/notes/two/)


---

1 min read · updated 2023-11-14 · [source](https://github.com/OWNER/REPO/blob/main/notes/one.md)
```

`note.md` sets `page_title` from keys the page defines, and `base.md` builds `title` from
`page_title`: keys resolve across the whole chain. `nav_title` keeps the series name out of the
links in `{{children}}` and prev/next. The extra blank lines come from the untaken `{{if}}` and
the blank lines around `{{content}}`; markdown ignores them.

### Table of contents in the skeleton

```html
{{if toc}}<nav class="toc">{{toc}}</nav>{{end}}
```

Shown only on pages with `##` headings. For a toc inside the page, put `{{toc}}` on its own
line in the body or a template; see [toc](#toc).

### Showing template syntax on a page

```
Write \{{title}} to print the title.
```

The `.md` output has `{{title}}`; in the HTML it reads `{{title}}`. Inside code blocks too.
