#!/bin/sh

set -eux

build=$1
suffix=$2

cd "$build"

strip muon
mv muon "muon-$suffix"
md5sum "muon-$suffix" > "muon-$suffix.md5"
