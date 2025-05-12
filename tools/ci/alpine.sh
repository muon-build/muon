#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

die_()
{
	printf "error: %s\n" "$@" >&2
	exit 1
}

sudo_()
{
	if [ "$cfg_sudo" ]; then
		sudo "$@"
	else
		"$@"
	fi
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
		add_repository_ "http://dl-cdn.alpinelinux.org/alpine/edge/community"
	fi

	if [ "$cfg_website" ]; then
		add_repository_ "http://dl-cdn.alpinelinux.org/alpine/v${alpine_version}/community"
	fi
}

install_packages_()
{
	packages=""

	queue_package_ build-base
	queue_package_ openssh

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
		-Dlibarchive=enabled \
		-Dlibcurl=enabled \
		-Dlibpkgconf=enabled \
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
		-Dlibarchive=disabled \
		-Dlibcurl=disabled \
		-Dlibpkgconf=disabled \
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

	if [ "$cfg_website" ]; then
		tools/ci/prepare_release_docs.sh "build"
	fi

	tools/ci/prepare_tarball.sh "muon-$version"
}

step_deploy_artifacts_()
{
	tools/ci/deploy.sh "/releases/$version" \
		"build/muon-$version-$arch-linux" \
		"build-small/muon-$version-$arch-linux-small" \
		"muon-$version.tar.gz"

	tools/ci/deploy.sh "/releases/$version" -r --delete build/doc/docs
	tools/ci/deploy.sh "/releases/$version"
}

step_deploy_website_()
{
	tools/ci/deploy.sh / -r --delete build/doc/website
	tools/ci/deploy.sh / -r --delete build/doc/book
}

step_copy_container_results_()
{
	cp \
		"build/version.sh" \
		"build/muon-$version-$arch-linux" \
		"build-small/muon-$version-$arch-linux-small" \
		"/home/output"
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

container_()
{
	rm -rf "build-docker"

	exec docker run \
		--rm \
		-i \
		-e "MUON_RUNNER_CONTAINER=1" \
		-v "$PWD:/home/muon-src:ro" \
		-v "$PWD/build-docker:/home/output" \
		-w "/home/muon" \
		"alpine:$alpine_version" \
		"/home/muon-src/tools/ci/alpine.sh"
}

alpine_version="3.21"

version=""
arch="$(arch)"
if [ "$arch" = "x86_64" ]; then
	arch=amd64
fi
runner=""
branch_name=""

cfg_sudo=""
cfg_tcc=""
cfg_clang=""
cfg_website=""
cfg_reuse=""
cfg_git_mirror=""
cfg_trigger_solaris_ci=""
cfg_deploy=""

if [ "${JOB_ID:-}" ]; then
	runner="builds.sr.ht"
	branch_name="$GIT_REF"

	cfg_sudo=1
	cfg_tcc=1
	cfg_clang=1
	cfg_website=1
	cfg_reuse=1
	cfg_git_mirror=1
	cfg_trigger_solaris_ci=1
	cfg_deploy=1
elif [ "${MUON_RUNNER_CONTAINER:-}" ]; then
	runner="container"
	branch_name=""

	apk add git
	git config --global --add safe.directory /home/muon/../muon-src/.git
	git config --global init.defaultBranch master
	git clone ../muon-src .
fi

if [ $# -ge 1 ]; then
	while getopts "hc" opt; do
		case "$opt" in
			h) usage_
				;;
			c) container_
				;;
			?) die "invalid option"
				;;
		esac
	done

	shift $((OPTIND-1))
fi

printf "\033[34mrunner: %s, arch: %s, branch_name: %s\033[0m\n" "$runner" "$arch" "$branch_name"

setup_repositories_
install_packages_

steps=""

# Since we are building static binaries, set this in the environment so muon
# doesn't complain
export PKG_CONFIG_PATH=/usr/lib/pkgconfig

# housekeeping steps
#####################

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

if [ "$cfg_deploy" ]; then
	# only allow master and release branches through
	if tools/ci/ref_is_release_branch.sh "$branch_name"; then :
		queue_step_ "deploy_artifacts"
	fi

	if [ "$cfg_website" ] && [ "$branch_name" = "refs/heads/master" ]; then :
		queue_step_ "deploy_website"
	fi
fi

if [ "$runner" = "container" ]; then
	queue_step_ "copy_container_results"
fi

run_steps_ $steps
