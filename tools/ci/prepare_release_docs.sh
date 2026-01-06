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
mv reference.html man.tar.gz docs
