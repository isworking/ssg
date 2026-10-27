#!/bin/sh
# usage: tests/docs_check.sh
# Fails if a subcommand or CLI flag in src/main.cpp is missing from docs/cli.md or man/ssg.1, or a site.toml key
# read in src/config.cpp is missing from docs/configuration.md or man/ssg.5.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
fail=0
# man pages escape a dash as \- (and \-\-), so compare with those removed from both sides.
flat() { sed -e 's/Fl \\-/--/g' -e 's/\\-/-/g' "$1"; }

# A flag must appear whole (not as the start of a longer flag); a key or command must be a
# backticked table cell (or `key =` line) in the markdown page, or an .It entry in the man page.
for cmd in $(grep -ohE 'name == "[a-z]+"\) return' src/main.cpp | sed 's/.*"\(.*\)".*/\1/' | sort -u) \
    render completions; do
  grep -q -e "^| \`$cmd[ \`]" docs/cli.md || { echo "docs_check: command $cmd is not in docs/cli.md"; fail=1; }
  grep -q -E -e "^\.(It )?Cm $cmd( |\$)" man/ssg.1 ||
    { echo "docs_check: command $cmd is not in man/ssg.1"; fail=1; }
done
for flag in $(grep -ohE -e '--[a-z][a-z-]+' src/main.cpp | sort -u); do
  for f in docs/cli.md man/ssg.1; do
    flat $f | grep -q -E -e "$flag([^a-z-]|\$)" || { echo "docs_check: $flag is not in $f"; fail=1; }
  done
done
for key in $(grep -ohE '(k|key) == "[a-z_]+"' src/config.cpp | sed 's/.*"\(.*\)"/\1/' | sort -u); do
  grep -q -E -e "^(\| \`$key\` \||$key *=)" docs/configuration.md ||
    { echo "docs_check: key '$key' is not in docs/configuration.md"; fail=1; }
  grep -q -E -e "^\.It Ic ([a-z_]+\.)*$key " man/ssg.5 || { echo "docs_check: key '$key' is not in man/ssg.5"; fail=1; }
done
# Every page the docs index links to must exist.
for f in $(grep -ohE '\]\([a-z0-9._/-]+\.md' docs/README.md | sed 's/](//'); do
  [ -f "docs/$f" ] || { echo "docs_check: docs/README.md links to missing $f"; fail=1; }
done
[ $fail = 0 ] && echo "docs_check: ok"
exit $fail
