#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/file.txt
test -f "$DESTDIR"/usr/generated.txt
test -f "$DESTDIR"/usr/wrapped.txt
test -f "$DESTDIR"/usr/wrapped2.txt
