#!/bin/sh
set -eu

BIN=${BIN:?set BIN}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cd "$tmp"
mkdir -p .rendercheck
printf 'old success\n' > .rendercheck/report.md
printf '{"passed":1}\n' > .rendercheck/results.json
printf '[project]\ncommand = "true"\nunknown_key = true\n' > rendercheck.toml

if "$BIN" run >/dev/null 2>&1; then
    echo 'invalid config unexpectedly passed' >&2
    exit 1
fi

test ! -e .rendercheck/report.md
test ! -e .rendercheck/results.json
