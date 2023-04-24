#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

muon="$1"
path="$2"

if ! "$muon" -v fmt -eq "$path"; then
	exit 1
fi
