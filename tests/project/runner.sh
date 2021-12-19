#!/bin/sh

set -eux

muon="$1"
ninja="$2"
source="$3"
build="$4"
skip_exit_code="$5"

mkdir -p "$build/muon-private"
log="$build/muon-private/build_log.txt"

set +e
"$muon" -v -C "$source" setup "$build" 2>"$log"
res=$?
set -e

cat "$log" >&2

if [ $res -ne 0 ]; then
	if grep -q "MESON_SKIP_TEST" "$log"; then
		exit "$skip_exit_code"
	else
		exit $res
	fi
fi

"$ninja" -C "$build"

"$muon" -C "$build" test
