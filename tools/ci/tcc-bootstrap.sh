#!/bin/sh

set -eu

git clone --depth 1 https://github.com/muon-build/tcc
cd tcc
cd win32
cmd //c build-tcc.bat
cd ..
./win32/tcc --version
