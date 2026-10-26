---
page_title: "Markdown features"
description: "Everything the renderer understands."
---
{{extend "base"}}

{{toc}}

## Tables

| Option | Default |
| :----- | ------: |
| html   | true    |
| scripts | false  |

## Footnotes

Plain text output too.[^txt]

[^txt]: Every page is also written as `.txt` and `.md`.

## Alerts

> [!NOTE]
> Alerts use GitHub's syntax.

## Definition lists

Skeleton
: The HTML shell every page is poured into.

## Task lists

- [x] tables
- [ ] world domination

## Other bits

~~Struck~~, ==marked==, and https://example.com autolinked. Template syntax can be
escaped: \{{children}} stays as written.
