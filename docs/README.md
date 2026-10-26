# ssg documentation

| | |
| --- | --- |
| [getting-started.md](getting-started.md) | Tutorial: a first site, templates, a series with navigation, a toc, `.assetignore`, building, previewing, deploying (Cloudflare Workers, nginx, plain static hosts) |
| [configuration.md](configuration.md) | `site.toml` reference, what gets published, `.assetignore` and `ignore` rules, nested checkouts, dist and cleaning, sitemap |
| [templates.md](templates.md) | Frontmatter, `{{key}}`, `{{extend}}`, `{{include}}`, `{{if}}`, escapes, built-in keys, navigation and sorting, the skeleton, drafts |
| [markdown.md](markdown.md) | The markdown dialect: CommonMark, GFM and other extensions, heading ids, raw HTML and sanitizing, the `.txt` output |
| [cli.md](cli.md) | `ssg` and `ssg render`: flags, exit codes, warnings and errors, environment |
| [../man/ssg.1](../man/ssg.1) | Manual page for the command (`man ssg`) |
| [../man/ssg.5](../man/ssg.5) | Manual page for the site format (`man 5 ssg`) |

The man pages are installed by `cmake --install build`. To read one from the source tree:
`man ./man/ssg.1`.

See also: [the README](../README.md) (build, install, quick start, tests) and
[`example/`](../example/), a complete site.

## Where to look

| question | page |
| --- | --- |
| Why is `foo.md` at `/foo/` and not `/foo.html`? | [getting-started.md](getting-started.md#where-pages-go) |
| Why wasn't a file published? | [configuration.md](configuration.md) |
| Why does `{{key}}` show up literally? | [templates.md](templates.md) |
| How are series sorted? | [templates.md](templates.md) |
| Is raw HTML allowed? Scripts? | [markdown.md](markdown.md) |
| What does this warning mean? | [cli.md](cli.md) |
| How do I serve `.md`/`.txt` by `Accept`? | [getting-started.md](getting-started.md#11-deploying) |
