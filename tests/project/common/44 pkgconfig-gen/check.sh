#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/include/simple.h
test -f "$DESTDIR"/usr/lib/libstat2.a
test -f "$DESTDIR"/usr/lib/pkgconfig/simple.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/libanswer.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/libfoo.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/libhello.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/libvartest.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/libvartest2.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/simple2.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/simple3.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/simple5.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/simple6.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/ct.pc
test -f "$DESTDIR"/usr/lib/pkgconfig/ct0.pc
test -f "$DESTDIR"/usr/share/pkgconfig/libhello_nolib.pc
