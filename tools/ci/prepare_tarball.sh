#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

setup_tmp_dirs_() {
	rm -rf "$tmp_dir"
	mkdir -p "$tmp_dir" "$tmp_dir/i"
}

git_archive_() {
	dir="$1"
	out="$2"
	rm -f "$out"
	git -C "$dir" archive --format=tar -o "$out" HEAD
}

archive_and_merge_() {
	i=0
	for dir in "$@"; do
		dest="$tmp_dir/i/$i.tar"
		git_archive_ "$dir" "$dest"

		mkdir -p "$tmp_dir/$final/$dir"
		tar x -C "$tmp_dir/$final/$dir" -f "$dest"
	done
}

create_final_archive_() {
	final_out="$tmp_dir/$final.tar.gz"
	rm -f "$tmp_dir/$final.tar" "$final_out"
	tar c -C "$tmp_dir" -f "$tmp_dir/$final.tar" "$final"
	gzip "$tmp_dir/$final.tar"
	mv "$final_out" .
	echo "wrote $final.tar.gz"
}

tmp_dir="$PWD/tarball_tmp"
final="$1"

setup_tmp_dirs_
archive_and_merge_ "." "subprojects/meson-tests" "subprojects/meson-docs"
create_final_archive_
rm -rf "$tmp_dir"
