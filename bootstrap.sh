#!/bin/sh

# Requirements:
# - c99
# - sh
# Optional requirements:
# - pkgconf or pkg-config
# - libpkgconf

set -eux

output="muon"

dir="${1:-}"
if [ -n "$dir" ]; then
	mkdir -p "$dir"
	output="$dir/$output"
fi

pkgconf_cmd=""
if command -v pkgconf >/dev/null; then
	pkgconf_cmd=pkgconf
elif command -v pkg-config >/dev/null; then
	pkgconf_cmd=pkg-config
fi

if [ -n "$pkgconf_cmd" ] && $pkgconf_cmd libpkgconf; then
	pkgconf_src="libpkgconf.c"
	pkgconf_cflags="$($pkgconf_cmd --cflags libpkgconf) -DBOOTSTRAP_HAVE_LIBPKGCONF"
	pkgconf_libs="$($pkgconf_cmd --libs libpkgconf)"
else
	pkgconf_src="libpkgconf_null.c"
	pkgconf_cflags=""
	pkgconf_libs=""
fi

${CC:-c99} -Iinclude $pkgconf_cflags "src/amalgam.c" $pkgconf_libs -o "$output"
