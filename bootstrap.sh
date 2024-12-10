#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

# Requirements:
# - c99
# - sh
# Optional requirements:
# - pkgconf or pkg-config
# - libpkgconf

set -eux

dir="$1"
mkdir -p "$dir"

pkgconf_cmd=""
if command -v pkgconf >/dev/null; then
	pkgconf_cmd=pkgconf
elif command -v pkg-config >/dev/null; then
	pkgconf_cmd=pkg-config
fi

if [ -n "$pkgconf_cmd" ] && $pkgconf_cmd libpkgconf; then
	pkgconf_cflags="$($pkgconf_cmd --cflags libpkgconf) -DBOOTSTRAP_HAVE_LIBPKGCONF"
	pkgconf_libs="$($pkgconf_cmd --libs libpkgconf)"
else
	pkgconf_cflags=""
	pkgconf_libs=""
fi

# shellcheck disable=SC2086
${CC:-c99} ${CFLAGS:-} ${LDFLAGS:-} -Iinclude $pkgconf_cflags "src/amalgam.c" $pkgconf_libs -o "$dir/muon-bootstrap"
