#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

build="$1"
shift

./bootstrap.sh "$build"

build/muon setup "$@" "$build"
build/muon -C "$build" samu
