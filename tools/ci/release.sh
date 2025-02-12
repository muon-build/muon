#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

build="$1"
build_small="$2"
version=""

# shellcheck disable=SC1091
. "$build/version.sh"

branch_name="$GIT_REF"

echo "version: $version, branch_name: '$branch_name'"

tools/ci/prepare_binary.sh "$build" "$version-amd64-linux-static"
tools/ci/prepare_binary.sh "$build_small" "$version-amd64-linux-static-small"
tools/ci/prepare_release_docs.sh "$build"
tools/ci/prepare_tarball.sh "muon-$version"

# only allow master and release branches through
if tools/ci/ref_is_release_branch.sh "$branch_name"; then :
else exit 0
fi

tools/ci/deploy.sh "/releases/$version" --mkpath -r --delete build/doc/docs
tools/ci/deploy.sh "/releases/$version" \
	"build/muon-$version-amd64-linux-static" \
	"build-small/muon-$version-amd64-linux-static-small" \
	"muon-$version.tar.gz"

# only allow master through
if [ "$branch_name" = "refs/heads/master" ]; then :
else exit 0
fi

tools/ci/deploy.sh / -r --delete build/doc/website
tools/ci/deploy.sh / -r --delete build/doc/book
