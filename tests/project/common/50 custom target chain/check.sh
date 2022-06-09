#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/subdir/data2.dat
test -f "$DESTDIR"/usr/subdir/data3.dat
