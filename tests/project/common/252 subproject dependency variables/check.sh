#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/pkgconfig/depvar_resource.pc
test -f "$DESTDIR"/usr/share/foo.c
test -f "$DESTDIR"/usr/share/subdir/foo.c
test -f "$DESTDIR"/usr/share/subdir2/foo.c
