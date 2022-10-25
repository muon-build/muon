#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

build=$1
suffix=$2

cd "$build"

strip muon
mv muon "muon-$suffix"
md5sum "muon-$suffix" > "muon-$suffix.md5"
