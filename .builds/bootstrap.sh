#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

# initial build
./bootstrap.sh "$1"

# get curl and zlib
build/muon setup "$1"
samu -C "$1"
