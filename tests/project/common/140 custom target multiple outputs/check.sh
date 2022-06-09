#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/include/diff.h
test -f "$DESTDIR"/usr/include/first.h
test -f "$DESTDIR"/usr/bin/diff.sh
test -f "$DESTDIR"/usr/bin/second.sh
test -f "$DESTDIR"/opt/same.h
test -f "$DESTDIR"/opt/same.sh
