#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/dir/file.txt
test -f "$DESTDIR"/usr/libtest/libstat.a
