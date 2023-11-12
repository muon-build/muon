#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

if [ ! -d ~/.ssh ]; then
	exit 0
elif [ "$(git rev-parse master)" != "$(git rev-parse HEAD)" ]; then
	exit 0
fi

sshopts="-o StrictHostKeyChecking=no -p 2975"

dest=$1
shift

rsync --rsh="ssh $sshopts" "$@" deploy@mochiro.moe:muon"$dest"
