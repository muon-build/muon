#!/bin/sh

set -eux

# initial build
./bootstrap.sh build

# get curl and zlib
build/muon setup build
samu -C build

# enable samurai wrap
build/muon setup -Dsamu=enabled build
samu -C build
