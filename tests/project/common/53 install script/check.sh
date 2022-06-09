#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/diiba/daaba/file.dat
test -f "$DESTDIR"/usr/this/should/also-work.dat
test -f "$DESTDIR"/usr/this/does/something-different.dat.in
test -f "$DESTDIR"/'usr/dir/a file.txt'
test -f "$DESTDIR"/usr/dir/conf.txt
test -f "$DESTDIR"/'usr/otherdir/a file.txt'
test -f "$DESTDIR"/usr/customtarget/1.txt
test -f "$DESTDIR"/usr/customtarget/2.txt
test -f "$DESTDIR"/usr/customtargetindex/1.txt
