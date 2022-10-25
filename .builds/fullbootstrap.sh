#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

.builds/bootstrap.sh build

# enable samurai wrap
build/muon setup -Dsamurai=enabled "$@" build
samu -C build
