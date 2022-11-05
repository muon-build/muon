#!/bin/sh -eu

archive="$1"

git archive --format=tar --prefix="$archive/" HEAD > "${archive}.tar"
find subprojects/samurai -type f | tar -u -T - \
	--transform="s|^|$archive/|g" \
	-f "${archive}.tar" \
	--owner=0 \
	--group=0

gzip -f "${archive}.tar"
