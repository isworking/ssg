---
page_title: "Blog{{if prev_page_url}}, page {{page_number}}{{end}}"
description: "Posts, two to a page."
paginate: "2"
---
{{extend "base"}}

{{children}}

{{if prev_page_url}}[Newer posts]({{prev_page_url}}){{end}}
{{if next_page_url}}[Older posts]({{next_page_url}}){{end}}

Browse by [tag](/tags/). Subscribe with the [Atom feed](/feed.xml).
