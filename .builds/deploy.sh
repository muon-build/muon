#!/bin/sh

set -eux

if [ "$(git rev-parse master)" != "$(git rev-parse HEAD)" ]; then
	exit 0
fi

sshopts="-o StrictHostKeyChecking=no -p 2975"

dest=$1
shift

rsync --rsh="ssh $sshopts" "$@" deploy@mochiro.moe:muon"$dest"
