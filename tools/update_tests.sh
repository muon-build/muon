#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

cmp_() {
	cmp "$@" 2>/dev/null 1>&2
}

remove_test_case_blocks_() {
	awk '
	BEGIN { skip=0 }
	/testcase expect_error/ { skip += 1 }
	/endtestcase/ { skip -= 1 }
	{
		if (skip > 0 || /endtestcase/) { printf "# " }
		print $0
	}
	'
}

format_() {
	remove_test_case_blocks_ < "$1" | $muon fmt -
}

copy_() {
	src_file="$1"
	dest="$2"
	meson_source="$3"

	printf "%s \033[32m->\033[0m %s\n" "$src_file" "$dest" 1>&2

	if [ "$meson_source" ]; then
		format_ "$src_file" > "$dest"
	else
		cp "$src_file" "$dest"
	fi
}

mkdir_dest_parent_()
{
	dest_dir="$1"
	relative="$2"
	dirname="${relative%/*}"
	if [ "$dirname" = "$relative" ]; then
		mkdir -p "$dest_dir"
	else
		mkdir -p "$dest_dir/$dirname"
	fi
}

handle_file_mod_() {
	d="$1"
	status="${d%%	*}"
	rename_dest=""
	file="${d#*	}"
	if [ "${status##R}" != "$status" ]; then
		status="R"
		a="${file%	*}"
		b="${d##*	}"

		file="$b"
		rename_dest="$a"

		src_file="$meson_root/$rename_dest"
		basename="${rename_dest##*/}"

		old_relative="${file##"$src_dir"/}"
		old_file="$dest_dir/$old_relative"

		relative="${rename_dest##"$src_dir"/}"
		dest="$dest_dir/$relative"
	else
		src_file="$meson_root/$file"
		basename="${file##*/}"

		relative="${file##"$src_dir"/}"
		dest="$dest_dir/$relative"

		old_relative="$relative"
		old_file="$dest_dir/$old_relative"
	fi

	meson_source=""
	if [ "$basename" = "meson.build" ] || [ "$basename" = "meson_options.txt" ]; then
		meson_source=1

		case "$relative" in
			"179 escape and unicode/meson.build")
				meson_source=""
				;;
		esac
	fi

	if [ ! -f "$dest" ] && [ "$status" != "R" ]; then
		if [ "$dryrun" ]; then
			printf "\033[32mnew\033[0m %s\n" "$relative"
		else
			mkdir_dest_parent_ "$dest_dir" "$relative"
			copy_ "$src_file" "$dest" "$meson_source"
		fi
	else
		changed=1
		if [ "$meson_source" ]; then
			if format_ "$src_file" | cmp_ "$old_file"; then
				changed=""
			fi
		else
			if cmp_ "$src_file" "$old_file"; then
				changed=""
			fi
		fi

		if [ "$changed" ] || [ "$status" = "R" ]; then
			if [ "$dryrun" ]; then
				msg=""
				if [ "$changed" ]; then
					msg="\033[35mmodified\033[0m "
				fi

				if [ "$status" = "R" ]; then
					msg="$msg\033[36mrenamed\033[0m "
					printf "${msg}%s -> %s\n" "$old_relative" "$relative"
				else
					printf "${msg}%s\n" "$relative"
				fi
			else
				if [ "$status" = "R" ]; then
					rm "$old_file"
					mkdir_dest_parent_ "$dest_dir" "$relative"
				fi

				copy_ "$src_file" "$dest" "$meson_source"
			fi
		fi
	fi
}

copy_tests_() {
	src_dir="$1"
	dest_dir="$tests_root/$2"

	git -C "$meson_root" ls-files -- "$src_dir" | while IFS="" read -r d; do
		handle_file_mod_ "A	$d"
	done
}

copy_tests_from_rev_() {
	src_dir="$1"
	dest_dir="$tests_root/$2"
	rev="$3"

	git -C "$meson_root" diff --name-status HEAD "$rev" -- "$src_dir" | while IFS="" read -r d; do
		handle_file_mod_ "$d"
	done
}

rev="1.5.1"

usage() {
	cat <<EOF
usage: $0 [opts] <path/to/meson> <path/to/meson-tests>
opts:
  -f - perform the copy (the default is dryrun only)
  -c - copy all tests, not just the ones modified since $rev
  -h - show this message
EOF
}

muon=muon
dryrun=1
copy_fun=copy_tests_from_rev_

if [ $# -ge 1 ]; then
	while getopts "fch" opt; do
		case "$opt" in
		f)
			dryrun=""
			;;
		c)
			copy_fun="copy_tests_"
			;;
		h)
			usage
			exit
			;;
		?) die "invalid option"
			;;
		esac
	done

	shift $((OPTIND-1))
fi

if [ $# -lt 1 ]; then
	usage
	exit 1
fi

meson_root="$1"
tests_root="$2"

"$copy_fun" "test cases/common" "common" "$rev"
"$copy_fun" "test cases/frameworks/6 gettext" "frameworks/6 gettext" "$rev"
"$copy_fun" "test cases/frameworks/7 gnome" "frameworks/7 gnome" "$rev"
"$copy_fun" "test cases/keyval" "keyval" "$rev"
"$copy_fun" "test cases/nasm" "nasm" "$rev"
"$copy_fun" "test cases/native" "native" "$rev"
"$copy_fun" "test cases/python" "python" "$rev"
"$copy_fun" "test cases/objc" "objc" "$rev"
"$copy_fun" "test cases/objcpp" "objcpp" "$rev"
