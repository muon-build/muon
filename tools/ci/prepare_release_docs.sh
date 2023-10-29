#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

build=$1
cd "$build/doc"

mkdir man
cp meson.build.5 muon.1 man
tar cvf man.tar man/*
gzip man.tar

mkdir docs
cp website/status.css docs
mv website/status.html man.tar.gz docs

rm -r website/version_info.py website/__pycache__
