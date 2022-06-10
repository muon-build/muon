#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/dircheck/fifth.dat
test -f "$DESTDIR"/usr/share/dircheck/seventh.dat
test -f "$DESTDIR"/usr/share/dircheck/ninth.dat
test -f "$DESTDIR"/usr/share/eighth.dat
test -f "$DESTDIR"/usr/share/fourth.dat
test -f "$DESTDIR"/usr/share/sixth.dat
test -f "$DESTDIR"/usr/share/sub1/data1.dat
test -f "$DESTDIR"/usr/share/sub1/second.dat
test -f "$DESTDIR"/usr/share/sub1/third.dat
test -f "$DESTDIR"/usr/share/sub1/sub2/data2.dat
test -f "$DESTDIR"/usr/share/sub2/one.dat
test -f "$DESTDIR"/usr/share/sub2/dircheck/excluded-three.dat
