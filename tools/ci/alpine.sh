#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

sudo_()
{
	sudo "$@"
}

################################################################################
# setup
################################################################################

queue_package_()
{
	packages="$packages $1"
}

add_repository_()
{
    printf "%s\n" "$1" | sudo_ tee -a /etc/apk/repositories
}

setup_repositories_()
{
	if [ "$cfg_tcc" ]; then
		add_repository_ "http://dl-cdn.alpinelinux.org/alpine/edge/testing"
	fi

	if [ "$cfg_website" ]; then
		add_repository_ "http://dl-cdn.alpinelinux.org/alpine/v${alpine_version}/community"
	fi
}

install_packages_()
{
	packages=""

	queue_package_ curl-dev
	queue_package_ libarchive-dev
	queue_package_ pkgconf-dev

	queue_package_ python3 # for meson-tests, meson-docs, and parts of the website
	queue_package_ linux-headers # used in a few project tests
	queue_package_ py3-yaml # for meson-docs

	if [ "$cfg_website" ]; then
		queue_package_ scdoc # for meson.build.5 and muon.1
		queue_package_ mandoc # for html man pages
		queue_package_ mdbook # for book
	fi

	if [ "$cfg_reuse" ]; then
		queue_package_ reuse # for licensing compliance
	fi

	# alternative c compilers
	if [ "$cfg_clang" ]; then
		queue_package_ clang
	fi
	if [ "$cfg_tcc" ]; then
		queue_package_ tcc
		queue_package_ tcc-libs-static # tcc 0.9.27_git20220323-r1 is broken without this
	fi

	# for static builds
	queue_package_ acl-static
	queue_package_ brotli-static
	queue_package_ bzip2-static
	queue_package_ curl-static
	queue_package_ expat-static
	queue_package_ libarchive-static
	queue_package_ lz4-static
	queue_package_ nghttp2-static
	queue_package_ openssl-libs-static
	queue_package_ xz-static
	queue_package_ zlib-static
	queue_package_ zstd-static
	queue_package_ libidn2-static
	queue_package_ libunistring-static
	queue_package_ libpsl-static

	# for releases
	queue_package_ rsync

	sudo_ apk add $packages
}

################################################################################
# steps
################################################################################

step_fix_static_libs_()
{
	# Because static libcares is installed as libcares_static, and this is
	# not listed in any .pc files, symlink it to the expected location.
	sudo_ ln -fs /usr/lib/libcares_static.a /usr/lib/libcares.a
}

step_reuse_lint_()
{
	reuse lint
}

step_push_to_gh_mirror_()
{
	tools/ci/push_to_gh_mirror.sh
}

step_trigger_solaris_ci_()
{
  if [ -d ~/.ssh ]; then
	tools/ci/solaris11.sh submit
  fi
}

enabled_if_()
{
	if [ "$1" ]; then
		printf "enabled"
	else
		printf "disabled"
	fi
}

step_build_gcc_()
{
	CC=gcc tools/ci/bootstrap.sh build \
		-Dbuildtype=release \
		-Dwerror=true \
		-Dpkgconfig=libpkgconf \
		-Dlibarchive=enabled \
		-Dlibcurl=enabled \
		-Dmeson-docs=enabled \
		-Dmeson-tests=enabled \
		-Dman-pages="$(enabled_if_ "$cfg_website")" \
		-Dwebsite="$(enabled_if_ "$cfg_website")" \
		-Dstatic=true
}

step_build_small_()
{
	CC=gcc build/muon setup \
		-Dbuildtype=minsize \
		-Dstatic=true \
		-Dlibcurl=disabled \
		-Dlibarchive=disabled \
		-Dpkgconfig=none \
		build-small
	build/muon -C build-small samu
}

step_build_tcc_()
{
	CC=tcc tools/ci/bootstrap.sh build-tcc
}

step_test_gcc_()
{
	CC=gcc build/muon -C build test -d dots
}

step_test_clang_()
{
	CC=clang build/muon -C build test -d dots
}

step_prepare_release_()
{
	# shellcheck disable=SC1091
	. "build/version.sh"

	printf "preparing release for version: %s, branch_name: '%s'\n" "$version" "$branch_name"

	tools/ci/prepare_binary.sh build "$version-$arch-linux"
	tools/ci/prepare_binary.sh build-small "$version-$arch-linux-small"

	if [ "$cfg_release_all" ]; then
		tools/ci/prepare_release_docs.sh "build"
		tools/ci/prepare_tarball.sh "muon-$version"
	fi
}

step_deploy_artifacts_()
{
	tools/ci/deploy.sh "/releases/$version" \
		"build/muon-$version-$arch-linux" \
		"build-small/muon-$version-$arch-linux-small"

	if [ "$cfg_release_all" ]; then
		tools/ci/deploy.sh "/releases/$version" -r --delete build/doc/docs
		tools/ci/deploy.sh "/releases/$version" "muon-$version.tar.gz"
	fi
}

step_deploy_website_()
{
	tools/ci/deploy.sh / -r --delete build/doc/website
	tools/ci/deploy.sh / -r --delete build/doc/book
}

################################################################################
# main
################################################################################

queue_step_()
{
	steps="$steps step_${1}_"
}

run_steps_()
{
	for step in "$@"; do
		printf "###################################\n"
		printf "# %-31s #\n" "$step"
		printf "###################################\n"
		"$step"
	done
}

alpine_version="3.31"

version=""
arch="$(arch)"
runner="local"
branch_name=""

if [ "${JOB_ID:-}" ]; then
	runner="builds.sr.ht"
elif [ "${CIRRUS_CI:-}" ]; then
	runner="cirrus-ci"
fi

case "$runner" in
	"builds.sr.ht")
		branch_name="$GIT_REF"
		;;
	"cirrus-ci")
		branch_name="$CIRRUS_BRANCH"
		;;
	*)
		branch_name="$(git rev-parse --abbrev-ref @)"
		;;
esac

cfg_default=""
if [ "$runner" = "builds.sr.ht" ]; then
	cfg_default="1"
fi

cfg_tcc="$cfg_default"
cfg_clang="$cfg_default"
cfg_website="$cfg_default"
cfg_reuse="$cfg_default"
cfg_git_mirror="$cfg_default"
cfg_trigger_solaris_ci="$cfg_default"
cfg_release_all="$cfg_default"

printf "runner: %s\n" "$runner"

setup_repositories_
install_packages_

steps=""

# Since we are building static binaries, set this in the environment so muon
# doesn't complain
export PKG_CONFIG_PATH=/usr/lib/pkgconfig

# housekeeping steps
#####################

# Fix static libs
queue_step_ "fix_static_libs"

if [ "$cfg_reuse" ]; then
	queue_step_ "reuse_lint"
fi

if [ "$cfg_git_mirror" ]; then
	queue_step_ "push_to_gh_mirror"
fi

if [ "$cfg_trigger_solaris_ci" ]; then
	queue_step_ "trigger_solaris_ci"
fi

# building steps
#####################

queue_step_ "build_gcc"

if [ "$cfg_tcc" ]; then
	queue_step_ "build_tcc"
fi

queue_step_ "build_small"

# testing steps
#####################

queue_step_ "test_gcc"

if [ "$cfg_clang" ]; then
	queue_step_ "test_clang"
fi

# release steps
#####################

queue_step_ "prepare_release"

# only allow master and release branches through
if tools/ci/ref_is_release_branch.sh "$branch_name"; then :
	queue_step_ "deploy_artifacts"
fi

if [ "$branch_name" = "refs/heads/master" ]; then :
	queue_step_ "deploy_website"
fi

run_steps_ $steps

