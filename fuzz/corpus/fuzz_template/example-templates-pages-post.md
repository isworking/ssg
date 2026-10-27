---
page_title: "{{series_title}}: {{post_title}}"
nav_title: "{{post_title}}"
---
{{extend "base"}}

{{content}}

---

{{if prev_url}}Previous: [{{prev_title}}]({{prev_url}}){{end}}
{{if next_url}}Next: [{{next_title}}]({{next_url}}){{else}}This is the latest post.{{end}}

{{include "partials/back"}}
