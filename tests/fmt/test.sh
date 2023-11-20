#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

muon="$1"

git -C "$MESON_SOURCE_ROOT" ls-files \
	| grep -v '\(editorconfig\|badnum.meson\)' \
	| grep '\(meson.build\|meson_options.txt\|.*.meson\)$' \
	| while read file; do
	path="$MESON_SOURCE_ROOT/$file"
	if ! "$muon" fmt -eq "$path"; then
		echo "$file"
		exit 1
	fi
done
