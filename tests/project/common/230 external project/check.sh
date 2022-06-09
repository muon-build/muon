#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/include/libfoo.h
test -f "$DESTDIR"/usr/lib/pkgconfig/somelib.pc
