#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

muon_private=".muon"

muon="$1"
ninja="$2"
source="$3"
build="$4"
skip_exit_code="$5"
skip_analyze="$6"
git_clean="$7"

if [ -d "$build" ]; then
	rm -rf "$build"
fi

if [ $git_clean -eq 1 ]; then
	git clean -xdf -- "$source"
fi

mkdir -p "$build/$muon_private"
log="$build/$muon_private/build_log.txt"

if [ $skip_analyze -eq 0 ]; then
	set +e
	"$muon" -v -C "$source" analyze 2>"$log"
	res=$?
	set -e
	cat "$log" >&2

	if [ $res -ne 0 ]; then
		exit "$res"
	fi
fi

set +e
"$muon" -v -C "$source" setup -Dprefix=/usr "$build" 2>"$log"
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

$ninja -C "$build"

"$muon" -C "$build" test

DESTDIR=destdir "$muon" -C "$build" install

if [ -x "$source/check.sh" ]; then
	set +e
	DESTDIR="$build/destdir" "$source/check.sh"
	res=$?
	set -e

	if [ $res -ne 0 ]; then
		set +x
		exec >&2
		if [ -d "$build/destdir" ]; then
			echo "failing destdir contains:"
			find "$build/destdir"
		else
			echo "failing destdir $build/destdir does not exist"
		fi
		exit $res
	fi
fi
