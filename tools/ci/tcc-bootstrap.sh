#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-FileCopyrightText: kzc <kzc@users.noreply.github.com>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

git clone --depth 1 https://github.com/muon-build/tcc
cd tcc
cd win32
cmd //c build-tcc.bat
cd ..
./win32/tcc --version
