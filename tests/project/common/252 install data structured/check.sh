#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/dir1/file1
test -f "$DESTDIR"/usr/share/dir1/file2
test -f "$DESTDIR"/usr/share/dir1/file3
test -f "$DESTDIR"/usr/share/dir2/file1
test -f "$DESTDIR"/usr/share/dir2/file2
test -f "$DESTDIR"/usr/share/dir2/file3
test -f "$DESTDIR"/usr/share/dir3/file1
test -f "$DESTDIR"/usr/share/dir3/file2
test -f "$DESTDIR"/usr/share/dir3/file3
