#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

cmp_() {
	cmp "$@" 2>/dev/null 1>&2
}

remove_test_case_blocks_() {
	in_block=0
	while IFS= read -r line; do
		case "$line" in
		*testcase*)
			in_block="$((in_block+1))"
			;;
		*endtestcase*)
			if [ "$in_block" -gt 0 ]; then
				in_block="$((in_block-1))"
			fi
			;;
		esac

		if [ "$in_block" -gt 0 ]; then
			printf "# "
		fi

		printf "%s\n" "$line"
	done
}

format_() {
	cat "$1" | remove_test_case_blocks_ |  $muon fmt -
}

copy_tests_() {
	src_dir="$1"
	dest_dir="$2"
	last_commit="$3"
	test_json_changed=""

	git -C "$meson_root" diff --name-status HEAD "$last_commit" -- "$src_dir" | while read d; do
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

			old_relative="${file##$src_dir/}"
			old_file="$dest_dir/$old_relative"

			relative="${rename_dest##$src_dir/}"
			dest="$dest_dir/$relative"
		else
			src_file="$meson_root/$file"
			basename="${file##*/}"

			relative="${file##$src_dir/}"
			dest="$dest_dir/$relative"

			old_relative="$relative"
			old_file="$dest_dir/$old_relative"
		fi

		if [ ! -f "$dest" ] && [ "$status" != "R" ]; then
			if [ "$dryrun" ]; then
				printf "\033[32mnew\033[0m %s\n" "$relative"
			else
				dirname="${relative%/*}"
				mkdir -p "$dest_dir/$dirname"
				cp "$src_file" "$dest"
			fi
		else
			changed=1
			meson_source=""
			if [ "$basename" = "meson.build" ] || [ "$basename" = "meson_options.txt" ]; then
				meson_source=1
				if format_ "$src_file" | cmp_ "$old_file"; then
					changed=""
				fi
			else
				if cmp_ "$src_file" "$old_file"; then
					changed=""
				fi
			fi

			if [ "$changed" ] || [ "$status" = "R" ]; then
				if [ "$changed" ] && [ "$basename" = "test.json" ]; then
					test_json_changed=1
				fi

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
						if [ -f "$old_file" ]; then
							rm "$old_file"
						fi
						dirname="${relative%/*}"
						mkdir -p "$dest_dir/$dirname"
					fi

					if [ "$meson_source" ]; then
						format_ "$src_file" > "$dest"
					else
						cp "$src_file" "$dest"
					fi
				fi
			fi
		fi
	done

	if [ "$test_json_changed" ]; then
		tools/generate_test_check_script.py "$dest_dir"
	fi
}

muon=muon
dryrun=1

usage() {
	cat <<EOF
usage: $0 [-f] <path/to/meson_repo>
EOF
}

if [ $# -ge 1 ]; then
	while getopts "fh" opt; do
		case "$opt" in
		f)
			dryrun=""
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

copy_tests_ "test cases/common" "tests/project/common" 1.0.1
copy_tests_ "test cases/nasm"   "tests/project/nasm"   1.0.1
copy_tests_ "test cases/native" "tests/project/native" 1.0.1
copy_tests_ "test cases/keyval" "tests/project/keyval" 1.0.1
