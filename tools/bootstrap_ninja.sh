#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

# Requirements:
# - c99
# - sh
# For automatic fetching of samurai sources:
# - curl or wget
# - tar capable of extracting a .tar.gz
# - mv

set -eux

dir="$1"
mkdir -p "$dir"

# Keep in sync with subprojects/samurai.wrap
source_filename="samurai-1.2-32-g81cef5d.tar.gz"
source_url="https://mochiro.moe/wrap/samurai-1.2-32-g81cef5d.tar.gz"

if [ ! -d subprojects/samurai ]; then
	if command -v curl >/dev/null; then
		curl -o "$dir/$source_filename" ${CURLOPTS:-} "$source_url"
	elif command -v wget >/dev/null; then
		wget -O "$dir/$source_filename" ${WGETOPTS:-} "$source_url"
	else
		set +x
		echo "Failed to automatically fetch samurai sources."
		echo "Please download and extract $source_url to subprojects/samurai."
		exit 1
	fi

	tar xvf "$dir/$source_filename"
	mv "samurai" "subprojects"
fi

${CC:-c99} ${CFLAGS:-} ${LDFLAGS:-} -Isubprojects/samurai subprojects/samurai/*.c -o "$dir/samu"
