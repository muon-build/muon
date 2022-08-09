#!/bin/sh

set -eu

cmp_() {
	cmp "$@" 2>/dev/null 1>&2
}

copy_tests_() {
	src_dir="$1"
	dest_dir="$2"
	last_commit="$3"
	test_json_changed=""

	git -C "$meson_root" diff --name-only HEAD "$last_commit" -- "$src_dir" | while read file; do
		src_file="$meson_root/$file"
		basename="${file##*/}"
		common="${file##$src_dir/}"
		dest="$dest_dir/$common"

		if [ ! -f "$dest" ]; then
			if [ "$dryrun" ]; then
				printf "\033[32mnew\033[0m %s\n" "$common"
			else
				dirname="${common%/*}"
				mkdir -p "$dest_dir/$dirname"
				cp "$src_file" "$dest"
			fi
		else
			if [ "$basename" = "meson.build" ] || [ "$basename" = "meson_options.txt" ]; then
				if $muon fmt_unstable "$src_file" | cmp_ "$dest"; then
					:
				else
					if [ "$dryrun" ]; then
						printf "\033[35mmodified\033[0m %s\n" "$common"
					else
						$muon fmt_unstable "$src_file" > "$dest"
					fi
				fi
			else
				if cmp_ "$src_file" "$dest"; then
					:
				else
					if [ "$dryrun" ]; then
						printf "\033[35mmodified\033[0m %s\n" "$common"
					else
						cp "$src_file" "$dest"

						if [ "$basename" = "test.json" ]; then
							test_json_changed=1
						fi
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

copy_tests_ "test cases/common" "tests/project/common" 3fbcff1c2722988d05c5248f7ab54c53001b1ee1
copy_tests_ "test cases/nasm"   "tests/project/nasm"   3fbcff1c2722988d05c5248f7ab54c53001b1ee1
copy_tests_ "test cases/native" "tests/project/native" 3fbcff1c2722988d05c5248f7ab54c53001b1ee1
