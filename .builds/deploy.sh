#!/bin/sh

set -eux

arch=$1

if [ "$(git rev-parse master)" = "$(git rev-parse HEAD)" ]; then
	sshopts="-o StrictHostKeyChecking=no -p 2975"
	rsync --rsh="ssh $sshopts" \
		"muon-$arch" \
		"muon-$arch.md5" \
		deploy@mochiro.moe:muon/
fi
