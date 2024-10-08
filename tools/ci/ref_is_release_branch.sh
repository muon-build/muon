#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

ref="$1"

if [ "$ref" = "refs/heads/master" ]; then exit 0
elif echo "$ref" | grep -q '^refs/heads/[0-9]\+\.[0-9]\+$'; then exit 0
else exit 1
fi
