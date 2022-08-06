#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/include/rootdir.h
test -f "$DESTDIR"/usr/include/subdir/subdir.h
test -f "$DESTDIR"/usr/include/subdir/childdir.h
test -f "$DESTDIR"/usr/include/subdir/child/childdir.h
test -f "$DESTDIR"/usr/include/vanished.h
test -f "$DESTDIR"/usr/include/fileheader.h
