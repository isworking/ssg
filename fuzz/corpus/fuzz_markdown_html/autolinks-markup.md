Markup characters inside a URL stay part of it: www.a.com/~user/ https://a.com/path?x=a~b
https://en.wikipedia.org/wiki/A_(b)_*c* and https://a.com/_x_ y and https://a.com/==m== c
and https://a.com/`x` y.

Around markup: *https://a.com/em* **www.b.com** _www.c.com_ ~~https://d.com~~ (www.e.com).

Escapes and entities: https://a.com/a\_b https://a.com/\*x\* https://a.com/?a=1&amp;b=2
www.f.com/he&lt;lp www.g.com.\< end. Entity-like tails (GFM spec):
www.google.com/search?q=commonmark&hl; and www.google.com/search?q=commonmark&hl=en.

A footnote after a URL: https://a.com/note[^n] and https://b.com/x^[inline].

In link text: [https://a.com/_x_](/dest) and [see www.h.com] unresolved.

Only lowercase: Www.cap.com WWW.X.COM HTTPS://UP.com Http://a.com www.ok.com https://ok.com

Unicode and escaped: <https://例え.jp/ü> and https://café.fr/menu and <http://x.com/a\b> and
<http://ok.com/a> and mail@ü.example.com and a@b.co.

[^n]: The note.
