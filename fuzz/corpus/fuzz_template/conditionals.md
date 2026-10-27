---
title: "Hi"
show: "true"
empty: ""
nested: "{{title}} and {{show}}"
loop: "{{loop}}"
---
{{if show}}shown {{nested}}{{else}}hidden{{end}}
{{if-not empty}}no value{{end}}
\{{title}} stays literal
{{loop}} {{url}}
{{include "partials/x"}}
