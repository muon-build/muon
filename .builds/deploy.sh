#!/bin/sh

set -eux

arch=$1

if [ "$(git rev-parse master)" != "$(git rev-parse HEAD)" ]; then
	exit 0
fi

sshopts="-o StrictHostKeyChecking=no -p 2975"

rsync --rsh="ssh $sshopts" \
	"muon-$arch" \
	"muon-$arch.md5" \
	deploy@mochiro.moe:muon/releases

rsync --rsh="ssh $sshopts" -r --delete \
	"doc/website" \
	deploy@mochiro.moe:muon
