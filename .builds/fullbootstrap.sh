#!/bin/sh

set -eux

.builds/bootstrap.sh build

# enable samurai wrap
build/muon setup -Dsamurai=enabled "$@" build
samu -C build
