# Markdown

ssg's dialect: CommonMark 0.31.2 (all 652 spec examples pass byte for byte in strict mode),
plus GFM-style extensions and a few of its own. Every page is rendered three ways from the same
source: HTML, plain text (`.txt`), and the resolved markdown itself (`.md`).

Try anything on this page with `ssg render` (see [docs/cli.md](cli.md)):

```
printf '~~old~~ ==new==\n' | ssg render -
printf '~~old~~ ==new==\n' | ssg render --text -
```

Output shown below is what the current binary emits with default options unless stated (long
examples trimmed with `...`).

Related: [docs/configuration.md](configuration.md) (`[markdown]` keys),
[docs/templates.md](templates.md) (`{{toc}}`, interpolation), [docs/cli.md](cli.md),
[docs/getting-started.md](getting-started.md), [docs/README.md](README.md).

- [Options](#options)
- [CommonMark](#commonmark)
- [Extensions](#extensions)
  - [Tables](#tables)
  - [Strikethrough](#strikethrough)
  - [Mark](#mark)
  - [Task lists](#task-lists)
  - [Footnotes](#footnotes)
  - [Definition lists](#definition-lists)
  - [Containers](#containers)
  - [Alerts](#alerts)
  - [Heading ids and attributes](#heading-ids-and-attributes)
  - [Bare autolinks](#bare-autolinks)
- [Raw HTML and sanitizing](#raw-html-and-sanitizing)
- [Plain-text output](#plain-text-output)
- [The .md output](#the-md-output)
- [CSS starter](#css-starter)
- [Limits](#limits)

## Options

Set in `site.toml` under `[markdown]` ([docs/configuration.md](configuration.md)). `ssg render`
has flags for some of them.

| key | default | effect | `render` flag |
| --- | --- | --- | --- |
| `html` | `true` | raw HTML passes through; `false` escapes it and shows it as text | `--no-html` |
| `scripts` | `false` | `false` sanitizes raw HTML (when `html` is on) and neutralizes dangerous link/image URLs ([below](#raw-html-and-sanitizing)) | `--scripts`, `--no-scripts` |
| `heading_ids` | `true` | `id="slug"` on every heading | none (`--strict` turns it off) |
| `autolinks` | `true` | bare `www.`/`http(s)://`/e-mail links; needs `extensions` | none (`--strict` turns it off) |
| `extensions` | `true` | everything in [Extensions](#extensions); `false` is CommonMark syntax only | `--no-extensions` |
| `text_width` | `80` | wrap column for `.txt`; `0` = no wrapping; `0`..`100000` | none (always 80) |

`extensions = false` turns off tables, strikethrough, mark, task lists, footnotes, definition
lists, containers, alerts, heading attributes and bare autolinks (even with `autolinks = true`).
Heading ids are a separate switch and stay on.

### Strict mode

`ssg render --strict` is plain CommonMark: `html` on, `scripts` on, `extensions`, `heading_ids`
and `autolinks` off. This is what the spec test runs (`tests/commonmark.py`, a ctest that
requires 100%). The site equivalent:

```toml
[markdown]
scripts = true
heading_ids = false
extensions = false
```

`--no-extensions` alone is not strict: headings still get ids and raw HTML is still sanitized.
With default options about 90% of the spec examples produce the spec's HTML; the rest differ
because of heading ids, bare autolinks, sanitizing and extension syntax.

## CommonMark

Everything in the [CommonMark 0.31.2 spec](https://spec.commonmark.org/0.31.2/) works as
specified: ATX and setext headings, paragraphs, block quotes, bullet and ordered lists, indented
and fenced code, thematic breaks, HTML blocks, link reference definitions, emphasis, links,
images, code spans, `<autolinks>`, raw inline HTML, hard breaks, backslash escapes, entities
(all 2125 HTML5 named references, plus numeric ones).

Input handling: `\n`, `\r\n` and lone `\r` all end a line. NUL becomes U+FFFD. Invalid UTF-8
is passed through as is. Tabs expand to 4-column stops where they
matter for structure.

The rules that most often surprise people:

- **Underscores are not intraword.** `snake_case_word` stays literal; `__init__` is
  `<strong>init</strong>`. `*` works inside words: `foo*bar*` gives `foo<em>bar</em>`.
- **Rule of 3.** A delimiter run that can both open and close can't match another one if the
  sum of their lengths is a multiple of 3, unless both are. `*foo**bar*` is one `<em>`.
- **Lists can't interrupt a paragraph with anything but `1`.** In `The year\n1999. was good`
  the second line is just paragraph text. An empty item can't interrupt a paragraph either.
- **Changing the bullet (`-`, `*`, `+`) or the ordered delimiter (`.`, `)`) starts a new list.**
- **Tight vs loose.** A list is loose (items wrapped in `<p>`) if any two items, or any two
  blocks inside an item, are separated by a blank line.
- **Item content indentation** is the marker width plus the spaces after it (1 to 4). Five or
  more spaces after the marker means one space, and the rest is indented code.
- **Indented code can't interrupt a paragraph**, so an indented line right after a paragraph
  line is a continuation.
- **`---` under a paragraph line is a setext `<h2>`**, not a rule. Put a blank line before it,
  or use `***`.
- **Lazy continuation.** A line without `>` that continues a paragraph still belongs to the
  quote's paragraph. Only paragraphs are lazy, not lists or code.
- **HTML blocks don't contain markdown** until a blank line ends them. `<div>\n*a*\n</div>` is
  emitted untouched; `<div>\n\n*a*\n\n</div>` gets `<p><em>a</em></p>` inside. Block types 6
  (known block tags) end at a blank line; type 7 (any other complete tag alone on its line)
  can't interrupt a paragraph.
- **Reference definitions are case-insensitive and the first one wins.** `[FOO]` matches
  `[Foo]: /first` even if `[foo]: /second` follows. Labels are whitespace-collapsed and
  case-folded (Latin, Greek, Cyrillic, fullwidth; `ß` folds to `ss`).
- **Hard breaks** are two or more trailing spaces or a trailing `\`; neither works at the end
  of a paragraph or heading.
- **`<scheme:...>` autolinks** need a 2-32 character scheme, so `<localhost:5001/foo>` is a link
  to `localhost:5001/foo`. No spaces allowed inside.

When `extensions` is on (the default), some plain CommonMark text means something else. If you
need it literal, escape the first character:

| looks like | becomes | literal form |
| --- | --- | --- |
| `Price` then `~ 5 USD` | definition list | `\~ 5 USD` |
| `Term` then `: text` | definition list | `\: text` |
| `2^[3]` | inline footnote | `2\^[3]` |
| `~x~`, `==x==` | `<del>`, `<mark>` | `\~x~`, `\==x==` |
| `::: name` line | container | `\::: name` |
| `## Title {#x}` | heading id `x` | `## Title \{#x}` |
| `www.x.com` | link | `` `www.x.com` `` |

## Extensions

All on by default, all off with `extensions = false` / `--no-extensions`.

### Tables

GFM pipe tables.

```
| Name  | Qty | Note   |
|:------|----:|:------:|
| apple |   3 | `a\|b` |
| pear  |  12 |
```

```html
<table>
<thead>
<tr>
<th align="left">Name</th>
<th align="right">Qty</th>
<th align="center">Note</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">apple</td>
<td align="right">3</td>
<td align="center"><code>a|b</code></td>
</tr>
<tr>
<td align="left">pear</td>
<td align="right">12</td>
<td align="center"></td>
</tr>
</tbody>
</table>
```

Rules:

- The header row is the last line of a paragraph; the delimiter row follows it. Lines before
  the header stay a paragraph (`Intro\n| a |\n|---|` is a `<p>` then a table).
- The delimiter row must contain at least one `|` and must start with `|`, `:` or `-`. Each
  cell is one or more `-`, optionally with `:` at either end (`:--` left, `--:` right, `:-:`
  center, `---` no `align`).
- The header must have exactly as many cells as the delimiter row, or it isn't a table.
- Outer pipes are optional: `a | b` / `- | -` is a table.
- Body rows with too many cells lose the extras; short rows are padded with empty cells.
- `\|` is a literal pipe inside a cell, also inside code spans. Only the backslash right before
  the pipe counts, so `\\|` is a backslash plus a literal pipe, not a separator (as in GFM).
- The table ends at a blank line or at any line that starts another block (heading, list, quote,
  fence, ...). Any other line, with or without pipes, is a row: `lazy row` becomes a one-cell
  row.
- A table with no body rows has no `<tbody>`.
- Tables are not continued lazily: inside `>`, a line without `>` ends the table.
- Cells hold inline markup only.
- Alignment is the `align` attribute, not a style. Use `th[align=right]` etc. in CSS.

### Strikethrough

`~one~` and `~~two~~` give `<del>`. Opener and closer must have the same length; `~~~three~~~`
stays literal. Flanking rules are those of `*`, so it works inside words: `a~~b~~c` gives
`a<del>b</del>c`.

```
~~strike~~ and ~single~ and ~~~three~~~
```

```html
<p><del>strike</del> and <del>single</del> and ~~~three~~~</p>
```

### Mark

`==text==` gives `<mark>`. Exactly two `=` on each side; `=x=`, `===x===` and spaced `== x ==`
stay literal. Nests with other emphasis: `**==b==**` is `<strong><mark>b</mark></strong>`. Not
in GFM.

### Task lists

```
- [ ] todo
- [x] done
- [X] also done
- [y] not a task
```

```html
<ul class="contains-task-list">
<li class="task-list-item"><input type="checkbox" disabled="" /> todo</li>
<li class="task-list-item"><input type="checkbox" checked="" disabled="" /> done</li>
<li class="task-list-item"><input type="checkbox" checked="" disabled="" /> also done</li>
<li>[y] not a task</li>
</ul>
```

- `[ ]`, `[x]` or `[X]` must start the item's first paragraph, on the marker's line, followed by
  whitespace or the end of the line. `- [ ]` with nothing after it is an (empty) task.
- The list (`<ul>` or `<ol>`) gets `contains-task-list` if any item is a task; only task items
  get `task-list-item`.
- In loose lists the checkbox is inside the `<p>`: `<li class="task-list-item">\n<p><input ... />
  loose</p>`.
- Checkboxes are always `disabled`.

### Footnotes

```
Ship it[^1]. Or inline^[Like *this*.]. Again[^1].

[^1]: The note.

    Indented 4: still the note.
```

```html
<p>Ship it<sup class="footnote-ref"><a href="#fn-1" id="fnref-1">1</a></sup>. Or inline<sup class="footnote-ref"><a href="#fn-inline-1" id="fnref-inline-1">2</a></sup>. Again<sup class="footnote-ref"><a href="#fn-1" id="fnref-1-2">1</a></sup>.</p>
<section class="footnotes">
<ol>
<li id="fn-1">
<p>The note.</p>
<p>Indented 4: still the note. <a href="#fnref-1" class="footnote-backref">↩</a> <a href="#fnref-1-2" class="footnote-backref">↩<sup>2</sup></a></p>
</li>
<li id="fn-inline-1">
<p>Like <em>this</em>. <a href="#fnref-inline-1" class="footnote-backref">↩</a></p>
</li>
</ol>
</section>
```

Definitions:

- `[^label]: text` at the start of a line (up to 3 spaces in). Following lines indented 4 or
  more spaces (and lazy paragraph lines) belong to it, so a footnote can hold several paragraphs,
  lists, code.
- Definitions can appear anywhere, before or after their references, even inside a quote or
  list. They are removed from where they stand; a quote left empty by that is dropped (a
  blank line left behind in a list can still make it loose).
- Labels match like reference labels: case-insensitive, whitespace collapsed. `[^My Note]` and
  `[^my  note]` are the same footnote. Labels can't contain `[`, `]` or a line break.
- Duplicate definitions: the first wins, the rest vanish.
- Unreferenced definitions are not rendered.

References:

- `[^label]` with no matching definition stays literal text: `Unused[^missing]`.
- `^[inline text]` defines and references a footnote in one go. Brackets inside must balance
  (`\]` escapes); it must close within 4096 bytes, else it's literal. Inline footnotes can hold
  other references and inline footnotes.
- Numbering follows first reference, main text first; references found inside footnotes come
  after all main-text ones.
- The `<section class="footnotes">` goes at the very end of the HTML, whatever the source order.
  It has no heading; add one in CSS (`::before`) or the template if you want it.

Ids:

- Footnote id = the label lowercased, with characters other than ASCII letters, digits, `-`,
  `_`, `.` and non-ASCII turned into `-` (runs collapse): `[^a b/c]` is `fn-a-b-c`. Inline
  footnotes are `inline-1`, `inline-2`, ... in source order.
- References: `fnref-ID` for the first, `fnref-ID-2`, `fnref-ID-3`, ... for later ones. Each
  gets its own back-link in the note: `↩`, `↩<sup>2</sup>`, ...
- If any of `fn-ID`, `fnref-ID`, `fnref-ID-k` would collide with a heading id or an earlier
  footnote's ids, the footnote's id gets `-2`, `-3`, ...: with a heading `# fn-x`, `[^x]`
  becomes `fn-x-2` / `fnref-x-2`.
- Back-links are appended to the note's last paragraph after a space. If the note ends with
  something else (a list, code), they go in a new `<p>` of their own.

Differences from GitHub: ids are `fn-`/`fnref-` without GitHub's `user-content-` prefix, there's
no "Footnotes" heading, and `^[inline]` footnotes don't exist on GitHub.

Known quirks: a reference inside link text produces an `<a>` nested in the link's `<a>`; a
reference inside image alt text renders as its number in `alt` but still counts as a reference,
so the note gets a back-link to an id that doesn't exist. Avoid both.

### Definition lists

```
Skeleton
: The HTML shell.
: Lives in `skeleton.html`.

dist
dist_dir

: Output directory.

  Emptied first when `clean = true`.
```

```html
<dl>
<dt>Skeleton</dt>
<dd>The HTML shell.</dd>
<dd>Lives in <code>skeleton.html</code>.</dd>
<dt>dist</dt>
<dt>dist_dir</dt>
<dd>
<p>Output directory.</p>
<p>Emptied first when <code>clean = true</code>.</p>
</dd>
</dl>
```

- A definition line is `:` or `~`, then at least one space or tab, then text, up to 3 spaces in.
  `:no-space` and a lone `:` are not definitions.
- Each line of the paragraph right before it becomes a `<dt>`; inline markup works in terms.
- The term paragraph may be followed by exactly one blank line (the definition is then loose).
  Two blank lines: no definition list, `: text` stays a paragraph.
- Continuation lines are indented to the definition's content column (2 for `: x`); they can
  hold any blocks. Unindented paragraph lines continue lazily.
- Tight definitions (no blank lines inside or before them) render inline: `<dd>text</dd>`. Loose
  ones wrap their paragraphs in `<p>`.
- Several definitions per term, several terms per definition. Consecutive term/definition groups
  merge into one `<dl>`, blank lines between them or not.

Not in GFM; the syntax follows PHP Markdown Extra / pandoc.

### Containers

Fenced with three or more colons. The opening line needs a name; anything after the name is a
title, rendered with inline markup.

```
::: warning Mind the *gap*
Body text.

:::: tip
Containers nest.
::::
:::
```

```html
<div class="alert warning">
<p class="alert-title">Mind the <em>gap</em></p>
<p>Body text.</p>
<div class="alert tip">
<p>Containers nest.</p>
</div>
</div>
```

- Class: `alert` plus the name lowercased, keeping only ASCII letters, digits, `-` and `_`
  (`Danger.Zone!` gives `alert dangerzone`; a name with none of those gives `note`). Any name
  works, not only the alert kinds.
- No title: no `<p class="alert-title">`.
- Closing line: at least as many colons as the opener, nothing else on the line. It closes the
  innermost open container whose fence is not longer than it. A `:::` inside a `:::::`
  container is just text. Equal-length fences nest fine: the first `:::` closes the inner one.
- A closing line inside an open fenced code block, or inside an unfinished HTML block that ends
  on a marker (`<script>`, `<pre>`, `<style>`, `<textarea>`, `<!--`, `<?`, `<!X`, CDATA),
  doesn't count, so code samples can show `:::`.
- A bare `:::` with no open container is a paragraph.
- Containers interrupt paragraphs, may be indented up to 3 spaces, and work inside quotes,
  lists and other containers. An unclosed container runs to the end of its parent (or the
  document).

Not in GFM; the syntax follows markdown-it-container / pandoc fenced divs (the output doesn't).

### Alerts

GitHub's syntax, ssg's markup (the same as a container's):

```
> [!NOTE]
> Useful information.

> [!caution]
> Lowercase works too.
```

```html
<div class="alert note">
<p class="alert-title">Note</p>
<p>Useful information.</p>
</div>
<div class="alert caution">
<p class="alert-title">Caution</p>
<p>Lowercase works too.</p>
</div>
```

- Kinds: `NOTE`, `TIP`, `IMPORTANT`, `WARNING`, `CAUTION`, any case. The title is the kind
  capitalized (`Note`, `Tip`, ...). Other kinds (`[!BOGUS]`) leave an ordinary blockquote.
- `[!KIND]` must be alone on the quote's first line. `> [!TIP] text` is an ordinary quote.
- The body may start on the next line or after a blank `>` line; an alert with no body is just
  the title.
- Alerts work anywhere a quote does: in lists, in containers, inside other alerts.

Differences from GitHub: GitHub emits `markdown-alert markdown-alert-note` classes and an icon,
and only renders alerts at the top level. ssg's `.alert.KIND` lets one stylesheet cover alerts
and containers.

### Heading ids and attributes

With `heading_ids = true` every heading gets a GitHub-style id:

```
# Hello World
## Hello World
## Hello, World!
## Ünïcödé — “Quotes” café’s 日本 😀
#
```

```html
<h1 id="hello-world">Hello World</h1>
<h2 id="hello-world-1">Hello World</h2>
<h2 id="hello-world-2">Hello, World!</h2>
<h2 id="ünïcödé--quotes-cafés-日本-">Ünïcödé — “Quotes” café’s 日本 😀</h2>
<h1 id="section"></h1>
```

Slug rules, applied to the heading's plain text (markup removed; code span contents, link text
and image alt text kept; footnote references and raw HTML tags dropped), trimmed:

- ASCII letters and digits are lowercased and kept; `-` and `_` are kept.
- Each space, and each Unicode space, becomes `-`. Runs are not collapsed: `a  b` is `a--b`.
- Other ASCII (punctuation, tabs) is dropped.
- Non-ASCII punctuation and symbols (Unicode P and S categories: `—`, `“`, `’`, emoji) are
  dropped. Other non-ASCII characters are kept as UTF-8, lowercased for Latin-1, Latin
  Extended-A and Additional, Greek, Cyrillic and fullwidth Latin (`ẞ` becomes `ß`, `İ` becomes
  `i`); others (CJK, ...) as written.
- An empty slug becomes `section`.

Dedupe: the first heading with a slug gets it as is; later ones get `-1`, `-2`, ... (counted
per slug), skipping any id already taken, including explicit ids anywhere in the document. Ids
of headings inside footnotes are assigned after all others.

Attributes: a trailing `{...}` sets the id and classes. It works on ATX and setext headings,
even with `heading_ids = false`:

```
### Custom {#my-id .big .red}
Setext {#set}
---
## A {#x} ##
```

```html
<h3 id="my-id" class="big red">Custom</h3>
<h2 id="set">Setext</h2>
<h2 id="x">A</h2>
```

- The braces must end the heading (after any closing `#`s are stripped). Space before `{` is
  optional.
- Inside: whitespace-separated tokens, each `#id` or `.class` with at least one character after
  the sigil. Anything else (`{}`, `{# }`, `{data=x}`, `{not attrs}`) means the whole thing is
  literal heading text.
- Several `#id`s: the last wins. Classes accumulate.
- `\{#no}` is literal; `\\{#yes}` is a literal backslash followed by attributes.
- Explicit ids are not checked or deduplicated: two `{#x}` headings both get `id="x"`.
  Generated ids avoid them.
- `extensions = false` disables attributes (the braces stay in the text and the slug).

With `heading_ids = false`, headings without `{#id}` get no id, and `{{toc}}` lists them as
plain text instead of links (see [docs/templates.md](templates.md)).

Differences from GitHub: no `user-content-` prefix; empty headings get `section`.

### Bare autolinks

GFM extended autolinks. Needs both `autolinks` and `extensions`.

```
Visit www.example.com, or https://example.com/path?q=1. Also
(https://en.wikipedia.org/wiki/Foo_(bar)) and foo@bar.example.com.
```

```html
<p>Visit <a href="http://www.example.com">www.example.com</a>, or <a href="https://example.com/path?q=1">https://example.com/path?q=1</a>. Also
(<a href="https://en.wikipedia.org/wiki/Foo_(bar)">https://en.wikipedia.org/wiki/Foo_(bar)</a>) and <a href="mailto:foo@bar.example.com">foo@bar.example.com</a>.</p>
```

What's recognised:

- `www.`, `http://`, `https://`, lowercase only: `Www.x.com`, `HTTPS://x.com`, `ftp://x.com`
  and `http://localhost` (no dot) are not links. `www.` links get `http://` in `href`.
- They must start a line or follow whitespace, `*`, `_`, `~` or `(`. `"www.x.com"` and
  `x.www.x.com` are not links.
- The domain needs at least one dot; segments are letters, digits, `-`, `_`, non-ASCII; no `_`
  in the last two segments (`www.x_y.z_w` is not a link).
- The link runs to whitespace or `<`, then trailing `? ! . , : * _ ~ ' "` are trimmed, a
  trailing `)` or `]` only if unbalanced within the link, and a trailing `&entity;`-like run.
  `www.commonmark.org/he&lt;lp` stops at the `&lt;`.
- Markup characters inside the URL stay in it: `https://a.com/_x_y_z` is one link, as are
  `~user`, `==m==`, `` `x` `` segments. A trailing one is still trimmed: `https://a.com/_x_y_`
  links `https://a.com/_x_y`. Escapes and entities in the URL are decoded in the link
  text and percent-encoded in `href`.
- A URL ends before a footnote reference: `https://a.com/note[^n]` links `https://a.com/note`.
- E-mail: `local@domain` where local is letters, digits, `.+-_`, the domain has a dot and
  doesn't end in `-` or `_`. Non-ASCII domains are not linked. `href` is `mailto:...`.
- Never inside code spans, existing links, or `<autolinks>`. Bare URLs inside raw HTML text
  (`<b>www.x.com</b>`, even `<a href=...>www.x.com</a>`) are linked.

Differences from GitHub: `mailto:` and `xmpp:` prefixes aren't recognised (`mailto:a@b.com`
links only `a@b.com`); an unbalanced trailing `]` is excluded (as in pandoc).

## Raw HTML and sanitizing

`html = true` (default) passes raw HTML through. `html = false` escapes it: an HTML block becomes
`<p>&lt;div&gt;...</p>`, inline tags show as text.

`scripts = false` (default) applies to raw HTML when `html` is on, and to markdown link and
image URLs always. It assumes content you wrote, not hostile input; it is a guard against
accidents and pasted embeds, not a full HTML sanitizer.

Markdown links and images:

- `javascript:`, `vbscript:`, `file:` URLs, and `data:` URLs other than `data:image/png`,
  `/gif`, `/jpeg`, `/webp` (so not SVG) are neutralized: links get `href="#"`, images
  `src=""`. Matching ignores case, whitespace and control characters.

Raw HTML is re-tokenized and rebuilt:

- These tags are escaped (shown as text, not removed): `applet base embed frame frameset iframe
  link meta noembed noframes noscript object plaintext portal script style template textarea
  title xmp`. So a pasted `<iframe>` embed or `<style>` block shows up as source.
- Dropped attributes: `on*` handlers, `srcdoc`, names with odd characters, any whose decoded
  value contains `javascript:`, `vbscript:` or `livescript:` anywhere, dangerous URLs as above,
  and `style` containing `expression(`, `behavior:` or `-moz-binding`.
- Attribute values are re-quoted with `"` and entity-decoded then re-escaped:
  `<a href=/ok class=c>` becomes `<a href="/ok" class="c">`.
- HTML comments are removed entirely (`<!-- more -->` vanishes).
- An unclosed tag escapes the rest of that HTML block.
- Everything else (`<details>`, `<kbd>`, `<svg>`, `<sup>`, `<a target>`, ...) passes.

```
<a href="https://x" target="_blank" onclick="x()">x</a> <iframe src="https://e"></iframe>
```

```html
<p><a href="https://x" target="_blank">x</a> &lt;iframe src=&quot;https://e&quot;&gt;&lt;/iframe&gt;</p>
```

`scripts = true` trusts everything: no escaping, no attribute or URL filtering.

## Plain-text output

The `.txt` file next to each page (and `ssg render --text`) is the page as readable text: no
markup, re-wrapped at `text_width`, UTF-8, every line right-trimmed, no leading or trailing
blank lines, exactly one final newline (an empty page gives an empty file). Blocks are separated
by one blank line.

Before:

````
# Release notes

Version **2.0** ships a [new parser](https://example.com/parser), a
[changelog](#changes) and ![a diagram](arch.png "Arch"). See https://example.com
or mail ops@example.com.\
This line follows a hard break.

### Changes

- Faster builds, which start to matter once a site grows past a few hundred pages
- Smaller output
  1. no trailing spaces
  2. one final newline
- [x] tests pass

> Quoted text keeps its marker on every wrapped line, even when the paragraph runs long.

```sh
ssg site/
```

<div class="note"><b>Raw</b> HTML &amp; entities<br>are reduced to text.</div>

***
````

After:

```
Release notes
=============

Version 2.0 ships a new parser <https://example.com/parser>, a changelog and
[image: a diagram] <arch.png>. See <https://example.com> or mail
<ops@example.com>.
This line follows a hard break.

### Changes

- Faster builds, which start to matter once a site grows past a few hundred
  pages
- Smaller output
  1. no trailing spaces
  2. one final newline
- [x] tests pass

> Quoted text keeps its marker on every wrapped line, even when the paragraph
> runs long.

    ssg site/

Raw HTML & entities
are reduced to text.

----------------------------------------
```

### Wrapping

- Paragraphs, list items, quotes, definitions, footnotes and HTML text are re-flowed: soft line
  breaks become spaces, runs of spaces collapse, and lines are filled greedily up to
  `text_width` columns. Hard breaks (`\`, two spaces, `<br>`) force a new line.
- Words longer than the width (long URLs) are never broken; they overflow.
- Nested content wraps at the parent width minus its prefix (`> `, `- `, `10. `, 4 for
  definitions, 2 for alerts), but never narrower than 20 columns. With a small `text_width`,
  nested blocks can be wider than top-level ones.
- `text_width = 0`: no wrapping at all; each paragraph is one line (hard breaks still break).
- Not wrapped: headings, table rows, code blocks, `<pre>` HTML.
- Code spans keep their spacing (`a    b` stays `a    b`); a single space inside one is still a
  wrap point.

### CJK and wide characters

East Asian wide and fullwidth characters count as 2 columns; combining marks as 0. This applies
to wrapping, table padding, heading underlines and list markers.

Text without spaces can break next to any wide character, except that line-start-forbidden
characters (`、。」』）！？ー` small kana, and ASCII `) ] } , . ! ? : ; %`) never begin a line
and opening brackets (`「『（` and ASCII `( [ {`) never end one.

```
これは「日本語」の文章です。これは「日本語」の文章です。...
```

```
これは「日本語」の文章です。これは「日本語」の文章です。これは「日本語」の文章で
す。これは「日本語」の文章です。...
```

### Blocks

| block | text form |
| --- | --- |
| `#` / `##` heading | text, then a line of `=` / `-` as wide as the text |
| `###`-`######` | `### text` (the `#`s kept) |
| empty heading | omitted |
| paragraph | wrapped |
| bullet list | `- ` for every bullet char; continuation lines indented by the marker width |
| ordered list | `N. ` or `N) ` as written, numbered from the start value; markers are not right-aligned (`9.` then `10.`) |
| task item | `- [ ] ` / `- [x] ` (`[X]` prints as `[x]`) |
| tight / loose list | no blank lines between items / blank lines |
| block quote | `> ` on every line, `>` on blank lines inside, `> > ` when nested |
| code block | indented 4 spaces, info string dropped, trailing whitespace stripped; empty blocks omitted |
| thematic break | `-` repeated `min(text_width, 40)` times (40 when `text_width = 0`) |
| table | columns padded to the widest cell, 2 spaces apart, honouring alignment; `-` rule under the header |
| definition list | terms flush left, definitions indented 4; a blank line before a term that follows a definition |
| container / alert | `[NAME]` or `[NAME] Title` (containers only; alerts don't repeat the kind), body indented 2 |
| footnotes | at the end: a blank line, a dash rule (as for thematic breaks), then each note as `[n] text`, continuation lines indented to match |
| HTML block | tags stripped, entities decoded; block-level tags (`div`, `p`, `h1`, `li`, `tr`, `hr`, ...) and `<br>` start new lines, `td`/`th` are separated by a space; `<script>`/`<style>` content and comments dropped; `<pre>` keeps its line layout |
| HTML block with `html = false` | printed verbatim, line by line |

Table:

```
| Name  | Qty | Note   |
|:------|----:|:------:|
| apple |   3 | `a\|b` |
| pear  |  12 |
```

```
Name   Qty  Note
-----  ---  ----
apple    3  a|b
pear    12
```

Definitions, containers, alerts (sources as in [Extensions](#extensions)):

```
Skeleton
    The HTML shell.
    Lives in skeleton.html.

dist
dist_dir
    Output directory.

    Emptied first when clean = true.

[WARNING] Mind the gap
  Body text.

  [TIP]
    Containers nest.

[NOTE]
  Useful information.
```

Footnotes (source as in [Footnotes](#footnotes)):

```
Ship it[1]. Or inline[2]. Again[1].

----------------------------------------

[1] The note.

    Indented 4: still the note.

[2] Like this.
```

### Inlines

| source | text |
| --- | --- |
| `**b**`, `*e*`, `~~s~~`, `==m==`, `` `c` `` | markers removed |
| `[text](https://x.com)` | `text <https://x.com>` |
| `[text](#anchor)`, `[text]()` | `text` |
| `<https://x.com>`, bare `https://x.com`, `www.x.com`, `a@b.c` | `<https://x.com>`, `<www.x.com>`, `<a@b.c>`: shown once, as written |
| `[a@b.c](mailto:a@b.c)` | `<a@b.c>`: shown once when the link text is the URL, with or without `mailto:` |
| `[mail me](mailto:a@b.c)` | `mail me <mailto:a@b.c>` |
| `![alt](cat.png "t")` | `[image: alt] <cat.png>` (title dropped) |
| neutralized URL (`javascript:` etc., `scripts = false`) | link text only; image without `<url>` |
| footnote reference | `[n]` |
| `&copy;`, `\*` | `©`, `*` |
| inline `<b>` etc. | tag dropped; `<br>` is a line break; `<script>`/`<style>` content dropped |
| inline HTML with `html = false` | shown as written |

Link titles are dropped. Links in headings and table cells use the same `text <url>` form.

## The .md output

Each page's `.md` (next to its `.html` and `.txt`) is the source after templating and before
markdown rendering. The HTML and the `.txt` are both rendered from exactly this text.

- Frontmatter removed.
- `{{extend}}` templates merged, `{{include}}` partials inserted, `{{key}}` values filled,
  `{{if}}` blocks resolved (see [docs/templates.md](templates.md)).
- `\{{` escapes turned into literal `{{`. The `.md` is meant to be read, not fed back to ssg.
- `{{toc}}` expanded into a markdown list on lines of its own, with blank lines around it:
  `##`-and-deeper headings, indented 2 spaces per level below the shallowest, link text
  markdown-escaped, linking to `#id` (plain items when the heading has no id).
- Blank lines at the top (left by templates) removed; ends with a newline.
- The markdown itself is untouched: footnotes, tables, raw HTML, everything stays as written.

Page `post.md`, with `templates/page.md` = `# {{title}}`, `{{toc}}`, `{{content}}`,
`{{include "partials/footer"}}` (each on its own line, blank lines between), and a footer
partial that says `_By {{author}}._`:

```
---
title: "Hello"
author: "Rajdeep"
---
{{extend "page"}}
## Setup
Write `\{{title}}` to print the title.
## Use `ssg`
Done[^1].

[^1]: Really.
```

`dist/post/index.md`:

```
# Hello

- [Setup](#setup)
- [Use ssg](#use-ssg)

## Setup
Write `{{title}}` to print the title.
## Use `ssg`
Done[^1].

[^1]: Really.


_By Rajdeep._
```

`dist/post/index.txt`, from the same text:

```
Hello
=====

- Setup
- Use ssg

Setup
-----

Write {{title}} to print the title.

Use ssg
-------

Done[1].

By Rajdeep.

----------------------------------------

[1] Really.
```

Template tags are interpolated everywhere in a page, including inside code spans and fenced
code. To show template syntax in a page, write `\{{`.

## CSS starter

Every class ssg emits, with neutral defaults. Adjust colours to taste.

```css
:root {
  --fg-muted: #6a737d;
  --border: #d0d7de;
  --note: #0969da;
  --tip: #1a7f37;
  --important: #8250df;
  --warning: #9a6700;
  --caution: #cf222e;
}
@media (prefers-color-scheme: dark) {
  :root {
    --fg-muted: #8b949e;
    --border: #30363d;
    --note: #4493f8;
    --tip: #3fb950;
    --important: #ab7df8;
    --warning: #d29922;
    --caution: #f85149;
  }
}

/* Alerts (> [!NOTE]) and containers (::: name). Unknown names fall back to --border. */
.alert {
  --c: var(--border);
  margin: 1em 0;
  padding: 0.5em 1em;
  border-left: 0.25em solid var(--c);
}
.alert.note { --c: var(--note); }
.alert.tip { --c: var(--tip); }
.alert.important { --c: var(--important); }
.alert.warning { --c: var(--warning); }
.alert.caution { --c: var(--caution); }
.alert > :first-child { margin-top: 0; }
.alert > :last-child { margin-bottom: 0; }
.alert-title { font-weight: 600; color: var(--c); }

/* Footnotes */
.footnote-ref a { text-decoration: none; }
.footnote-ref a::before { content: "["; }
.footnote-ref a::after { content: "]"; }
.footnotes {
  margin-top: 2em;
  border-top: 1px solid var(--border);
  font-size: 0.9em;
}
.footnote-backref { text-decoration: none; }
:target { scroll-margin-top: 1em; }
.footnotes li:target { background: color-mix(in srgb, var(--note) 10%, transparent); }

/* Task lists */
.contains-task-list { padding-left: 1.2em; }
.task-list-item { list-style: none; }
.task-list-item > input { margin: 0 0.4em 0 -1.2em; vertical-align: middle; }
.task-list-item > p > input { margin: 0 0.4em 0 -1.2em; }

/* Tables: alignment comes as align="left|center|right" */
table { border-collapse: collapse; }
th, td { border: 1px solid var(--border); padding: 0.3em 0.6em; }
th[align="left"], td[align="left"] { text-align: left; }
th[align="center"], td[align="center"] { text-align: center; }
th[align="right"], td[align="right"] { text-align: right; }

/* Definition lists */
dt { font-weight: 600; }
dd { margin: 0 0 0.5em 1.5em; }

/* ==mark== and ~~del~~ */
mark { padding: 0 0.15em; }
del { color: var(--fg-muted); }

/* Fenced code: <pre><code class="language-xyz"> */
pre { overflow-x: auto; }
```

Fenced code with an info string gets `class="language-WORD"` (first word only). ssg doesn't
highlight; add a client-side highlighter if you want one.

## Limits

| what | limit | beyond it |
| --- | --- | --- |
| block nesting (quotes, lists, containers, footnotes, definitions) | 100 levels | the marker stays text |
| inline footnote `^[...]` | 4096 bytes from `^` | literal text |
| footnote label | under 1000 bytes, one line, no `[`/`]` | not a footnote |
| link reference label | 999 characters (spec) | not a link |
| ordered list start | 9 digits (spec) | not a list |
| bare autolink domain | 1024 bytes | not a link |
| `text_width` | `0`..`100000` | config error |

Pathological inputs (deep nesting, delimiter floods, long runs of `<!--`, `^[`, `[`, tens of
thousands of bare URLs) render in well under a second at 100 KB.
