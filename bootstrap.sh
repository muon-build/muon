#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

# Requirements:
# - c99
# - sh

set -eux

dir="$1"
mkdir -p "$dir"

# shellcheck disable=SC2086
${CC:-c99} ${CFLAGS:-} ${LDFLAGS:-} -Iinclude "src/amalgam.c" -o "$dir/muon-bootstrap"
