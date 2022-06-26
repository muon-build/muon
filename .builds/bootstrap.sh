#!/bin/sh

set -eux

# initial build
./bootstrap.sh "$1"

# get curl and zlib
build/muon setup "$1"
samu -C "$1"
