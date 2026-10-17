#!/bin/sh
set -eu
BIN="$(cd "$(dirname "$0")/.." && pwd)/lameorg"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP" "$TMP.dry.out" "$TMP.run.out" "$TMP.undo.out"' EXIT HUP INT TERM
cd "$TMP"
printf pdf > report.PDF
printf archive > bundle.tar.gz
printf c > main.c
printf plain > README
printf hidden > .secret.txt
mkdir existing-dir pdf
printf stay > existing-dir/inside.txt
printf collision > pdf/report.PDF
"$BIN" --dry-run > "$TMP.dry.out"
test -f report.PDF
grep -q 'WOULD bundle.tar.gz -> tar.gz/bundle.tar.gz' "$TMP.dry.out"
"$BIN" > "$TMP.run.out"
test -f pdf/report.PDF
test -f tar.gz/bundle.tar.gz
test -f c/main.c
test -f .secret.txt
test -f existing-dir/inside.txt
grep -q 'SKIP  report.PDF' "$TMP.run.out"
MANIFEST=$(sed -n 's/^Undo manifest: //p' "$TMP.run.out")
test -n "$MANIFEST"
"$BIN" --undo "$MANIFEST" > "$TMP.undo.out"
test -f bundle.tar.gz
test -f main.c
test ! -e tar.gz/bundle.tar.gz
test ! -e c/main.c
echo 'PASS: dry-run, compound extensions, hidden-file exclusion, directories, collision safety, undo'
