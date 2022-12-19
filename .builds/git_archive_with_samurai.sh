#!/bin/sh -eu
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

archive="$1"

git archive --format=tar --prefix="$archive/" HEAD > "${archive}.tar"
find subprojects/samurai -type f | tar -u -T - \
	--transform="s|^|$archive/|g" \
	-f "${archive}.tar" \
	--owner=0 \
	--group=0

gzip -f "${archive}.tar"
