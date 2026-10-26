#!/usr/bin/env python3
"""Run the CommonMark spec examples against `ssg render --strict`.

usage: tests/commonmark.py path/to/ssg path/to/spec.json [--section NAME] [--verbose] [--example N]

Get spec.json from https://spec.commonmark.org/0.31.2/spec.json
"""
import argparse
import html
import json
import re
import subprocess
import sys
from html.parser import HTMLParser


class Normalizer(HTMLParser):
    """Loose HTML normalization in the spirit of the reference test suite's normalizer."""

    BLOCK = {"article", "header", "aside", "hgroup", "blockquote", "hr", "iframe", "body", "li",
             "map", "button", "object", "canvas", "ol", "caption", "output", "col", "p", "colgroup",
             "pre", "dd", "progress", "div", "section", "dl", "table", "td", "dt", "tbody", "embed",
             "textarea", "fieldset", "tfoot", "figcaption", "th", "figure", "thead", "footer", "tr",
             "form", "ul", "h1", "h2", "h3", "h4", "h5", "h6", "video", "script", "style"}

    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.out = []
        self.in_pre = False
        self.last = "starttag"
        self.last_tag = ""

    def handle_data(self, data):
        after_tag = self.last in ("endtag", "starttag")
        after_block = self.last_tag in self.BLOCK
        if after_tag and self.last_tag == "br":
            data = data.lstrip("\n")
        if not self.in_pre:
            data = re.sub(r"\s+", " ", data)
        if after_tag and after_block and not self.in_pre:
            if self.last == "starttag":
                data = data.lstrip()
            elif self.last == "endtag":
                data = data.strip()
        self.out.append(data)
        self.last = "data"

    def handle_endtag(self, tag):
        if tag == "pre":
            self.in_pre = False
        elif tag in self.BLOCK:
            if self.out and self.last == "data":
                self.out[-1] = self.out[-1].rstrip()
        self.out.append("</" + tag + ">")
        self.last_tag = tag
        self.last = "endtag"

    def handle_starttag(self, tag, attrs):
        if tag == "pre":
            self.in_pre = True
        if tag in self.BLOCK and self.out and self.last == "data":
            self.out[-1] = self.out[-1].rstrip()
        self.out.append("<" + tag)
        for k, v in sorted(attrs):
            self.out.append(" " + k)
            if v is not None:
                self.out.append('="' + html.escape(v, quote=True) + '"')
        self.out.append(">")
        self.last_tag = tag
        self.last = "starttag"

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_comment(self, data):
        self.out.append("<!--" + data + "-->")
        self.last = "comment"

    def handle_decl(self, data):
        self.out.append("<!" + data + ">")
        self.last = "decl"

    def unknown_decl(self, data):
        self.out.append("<!" + data + ">")
        self.last = "decl"

    def handle_pi(self, data):
        self.out.append("<?" + data + ">")
        self.last = "pi"

    def handle_entityref(self, name):
        try:
            c = html.unescape("&" + name + ";")
            self.out.append(html.escape(c, quote=False) if c != "&" + name + ";" else "&" + name + ";")
        except Exception:
            self.out.append("&" + name + ";")
        self.last = "ref"

    def handle_charref(self, name):
        try:
            c = html.unescape("&#" + name + ";")
            self.out.append(html.escape(c, quote=False))
        except Exception:
            self.out.append("&#" + name + ";")
        self.last = "ref"


def normalize(s):
    p = Normalizer()
    try:
        p.feed(s)
        p.close()
    except Exception:
        return s.strip()
    return "".join(p.out).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ssg")
    ap.add_argument("spec")
    ap.add_argument("--section")
    ap.add_argument("--example", type=int)
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--min", type=float, default=0.0, help="fail if pass rate (%%) is below this")
    a = ap.parse_args()

    examples = json.load(open(a.spec, encoding="utf-8"))
    by_section = {}
    passed = 0
    run = 0
    for ex in examples:
        if a.section and ex["section"] != a.section:
            continue
        if a.example and ex["example"] != a.example:
            continue
        run += 1
        try:
            r = subprocess.run([a.ssg, "render", "--strict", "-"], input=ex["markdown"].encode(),
                               capture_output=True, timeout=10)
            got = r.stdout.decode("utf-8", "replace")
            crashed = r.returncode != 0
        except subprocess.TimeoutExpired:
            got, crashed = "<<timeout>>", True
        ok = not crashed and (got == ex["html"] or normalize(got) == normalize(ex["html"]))
        s = by_section.setdefault(ex["section"], [0, 0])
        s[1] += 1
        if ok:
            passed += 1
            s[0] += 1
        elif a.verbose:
            print(f"--- example {ex['example']} ({ex['section']}){' CRASH' if crashed else ''}")
            print("markdown:", repr(ex["markdown"]))
            print("expected:", repr(ex["html"]))
            print("got:     ", repr(got))
    for name, (p, t) in sorted(by_section.items(), key=lambda kv: kv[1][0] - kv[1][1]):
        print(f"{p:4d}/{t:<4d} {name}")
    rate = 100.0 * passed / run if run else 0.0
    print(f"{passed}/{run} passed ({rate:.1f}%)")
    sys.exit(1 if rate < a.min else 0)


if __name__ == "__main__":
    main()
