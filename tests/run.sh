#!/bin/sh
# usage: tests/run.sh path/to/ssg [--update]
#
# 1. Builds example/ and diffs it against tests/expected/example/ (--update rewrites that).
# 2. Runs every tests/sites/<name>.check: a sh snippet that calls `build` on a scratch copy of
#    tests/sites/<name>/ and then asserts with the helpers below.
# 3. Renders every tests/markdown/X.md and compares it with X.html / X.txt (never --update'd).
set -u

[ $# -ge 1 ] || { echo "usage: $0 path/to/ssg [--update]" >&2; exit 2; }
case $1 in /*) SSG=$1 ;; *) SSG=$(pwd)/$1 ;; esac
UPDATE=0
[ "${2:-}" = "--update" ] && UPDATE=1
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ssg-test.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM
export SOURCE_DATE_EPOCH=0  # build_date = 1970-01-01

pass=0
fail=0
NAME=""

bad() {
  echo "FAIL [$NAME]: $*"
  fail=$((fail + 1))
}
ok() { pass=$((pass + 1)); }

# build [ssg args...]: build $SITE; sets $rc, stderr goes to $ERR.
build() {
  "$SSG" "$@" "$SITE" >"$TMP/$NAME.out" 2>"$ERR"
  rc=$?
}
status() { [ "$rc" -eq "$1" ] && ok || bad "exit status $rc, want $1 ($(cat "$ERR"))"; }
has() { [ -e "$DIST/$1" ] && ok || bad "missing $1"; }
lacks() { [ ! -e "$DIST/$1" ] && ok || bad "unexpected $1"; }
# contains FILE TEXT / omits FILE TEXT: fixed-string search in a dist file.
contains() { grep -qF -- "$2" "$DIST/$1" 2>/dev/null && ok || bad "$1 lacks: $2"; }
omits() { [ -e "$DIST/$1" ] && ! grep -qF -- "$2" "$DIST/$1" && ok || bad "$1 has: $2"; }
err_has() { grep -qF -- "$1" "$ERR" && ok || bad "stderr lacks: $1 (got: $(cat "$ERR"))"; }
err_lacks() { ! grep -qF -- "$1" "$ERR" && ok || bad "stderr has: $1"; }
# same FILE EXPECTED-TEXT: whole-file comparison.
same() {
  printf '%s' "$2" >"$TMP/want"
  if cmp -s "$TMP/want" "$DIST/$1"; then ok; else
    bad "$1 differs:"
    diff "$TMP/want" "$DIST/$1" | sed 's/^/    /'
  fi
}

# minimal_site: a bare site.toml, skeleton and templates/ in $SITE.
minimal_site() {
  mkdir -p "$SITE/templates"
  printf 'dist = "dist"\ntemplates = "templates"\nskeleton = "skeleton.html"\n' >"$SITE/site.toml"
  printf '<html><head><title></title></head><body><main></main></body></html>\n' >"$SITE/skeleton.html"
}
# src_same FILE EXPECTED-TEXT: whole-file comparison of a file in $SITE (not dist).
src_same() {
  printf '%s' "$2" >"$TMP/want"
  cmp -s "$TMP/want" "$SITE/$1" && ok || bad "$SITE/$1 changed: $(cat "$SITE/$1" 2>&1)"
}

# --- example/ golden files
NAME=example
SITE=$TMP/example
DIST=$SITE/dist
ERR=$TMP/example.err
cp -R "$ROOT/example" "$SITE"
rm -rf "$DIST"
build --quiet
status 0
if [ "$UPDATE" -eq 1 ]; then
  rm -rf "$HERE/expected/example"
  mkdir -p "$HERE/expected"
  cp -R "$DIST" "$HERE/expected/example"
  echo "updated tests/expected/example"
elif diff -r "$HERE/expected/example" "$DIST" >"$TMP/example.diff"; then
  ok
else
  bad "dist differs from tests/expected/example (run with --update if intended):"
  sed 's/^/    /' "$TMP/example.diff" | head -60
fi

# --- focused fixtures
for check in "$HERE"/sites/*.check; do
  [ -e "$check" ] || continue
  NAME=$(basename "$check" .check)
  SITE=$TMP/sites/$NAME
  DIST=$SITE/dist
  ERR=$TMP/$NAME.err
  mkdir -p "$TMP/sites"
  if [ -d "$HERE/sites/$NAME" ]; then cp -R "$HERE/sites/$NAME" "$SITE"; else mkdir -p "$SITE"; fi
  before=$fail
  . "$check"
  [ "$fail" -eq "$before" ] && echo "ok   $NAME" || echo "FAIL $NAME"
done

# --- markdown/X.md: `ssg render` must give X.html, `ssg render --text` X.txt (hand-verified, so
# --update leaves them alone)
for md in "$HERE"/markdown/*.md; do
  [ -e "$md" ] || continue
  NAME=markdown/$(basename "$md" .md)
  before=$fail
  for kind in html txt; do
    want=${md%.md}.$kind
    [ -e "$want" ] || continue
    if [ $kind = txt ]; then "$SSG" render --text "$md" >"$TMP/got" 2>"$TMP/got.err"
    else "$SSG" render "$md" >"$TMP/got" 2>"$TMP/got.err"; fi
    rc=$?
    if [ $rc -eq 0 ] && cmp -s "$want" "$TMP/got"; then ok; else
      bad "render$([ $kind = txt ] && echo ' --text') differs from $(basename "$want") (exit $rc):"
      diff "$want" "$TMP/got" | head -20 | sed 's/^/    /'
      sed 's/^/    /' "$TMP/got.err"
    fi
  done
  [ "$fail" -eq "$before" ] && echo "ok   $NAME" || echo "FAIL $NAME"
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
