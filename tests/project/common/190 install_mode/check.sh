#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/bin/runscript.sh
test -f "$DESTDIR"/usr/include/config.h
test -f "$DESTDIR"/usr/include/rootdir.h
test -f "$DESTDIR"/usr/libtest/libstat.a
test -f "$DESTDIR"/usr/share/man/man1/foo.1
test -f "$DESTDIR"/usr/share/sub1/second.dat
test -f "$DESTDIR"/usr/share/sub2/stub
test -f "$DESTDIR"/usr/subdir/data.dat
