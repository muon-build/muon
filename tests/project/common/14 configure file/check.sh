#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/appdir/config2.h
test -f "$DESTDIR"/usr/share/appdir/config2b.h
test -f "$DESTDIR"/usr/share/appdireh/config2-1.h
test -f "$DESTDIR"/usr/share/appdirok/config2-2.h
