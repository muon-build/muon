#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

build_log="$HOME/build_log.txt"

send_status() {
	echo "$1" > status
	rsync "$build_log" status deploy@mochiro.moe:muon/ci-results/solaris11/
}

build() {
	date
	uname -a

	set -x

	git clone https://git.sr.ht/~lattis/muon "$tmp"
	cd "$tmp"
	git checkout "$1"

	export CC=gcc
	export CFLAGS="-D_POSIX_C_SOURCE=200112L -D__EXTENSIONS__"

	./bootstrap.sh build
	build/muon-bootstrap setup -Dlibarchive=disabled build
	build/muon-bootstrap -C build samu
	build/muon -C build test -d dots -s lang
}

submit() {
	cat tools/ci/solaris11.sh | ssh \
		-oPubkeyAcceptedKeyTypes=+ssh-rsa \
		-oStrictHostKeyChecking=no \
		-oHostKeyAlgorithms=ssh-rsa \
		lattis@gcc211.fsffrance.org \
			nohup /bin/sh -s receive "$(git rev-parse @)"
}

_receive() {
	echo "build $1 received, logging to $build_log"
	exec >"$build_log"
	exec 2>&1
	send_status pending

	tmp="ci/muon/$(date +%s)"
	mkdir -p "$tmp"

	if build "$1"; then
		send_status ok
	else
		send_status failed
	fi

	cd
	rm -rf "$tmp"
}

receive() {
	_receive "$@"&
}

command="$1"
shift
$command "$@"
