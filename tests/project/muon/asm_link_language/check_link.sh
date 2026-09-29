#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only
#
# Asserts that both asm+c targets link through the C driver rule, independent of
# source order. Runs with cwd = build root, so build.ninja is right here.

set -eu

ninja=build.ninja
if [ ! -f "$ninja" ]; then
	echo "check_link: $ninja not found in $(pwd)" >&2
	exit 1
fi

rc=0
for tgt in asmc casm; do
	line=$(grep -E "^build ${tgt}:" "$ninja" || true)
	if [ -z "$line" ]; then
		echo "check_link: no build edge for '$tgt'" >&2
		rc=1
	elif echo "$line" | grep -q 'assembly_linker'; then
		echo "check_link: FAIL '$tgt' links as assembly, expected C: $line" >&2
		rc=1
	elif echo "$line" | grep -q 'c_linker'; then
		echo "check_link: OK '$tgt' links as C"
	else
		echo "check_link: FAIL '$tgt' unexpected link rule: $line" >&2
		rc=1
	fi
done

exit $rc
