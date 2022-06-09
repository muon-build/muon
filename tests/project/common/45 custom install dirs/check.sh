#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/some/dir/sample.h
test -f "$DESTDIR"/usr/some/dir2/sample.h
test -f "$DESTDIR"/usr/woman/prog.1
test -f "$DESTDIR"/usr/woman2/prog.1
test -f "$DESTDIR"/usr/meow/datafile.cat
test -f "$DESTDIR"/usr/meow2/datafile.cat
test -f "$DESTDIR"/usr/woof/subdir/datafile.dog
test -f "$DESTDIR"/usr/woof2/subdir/datafile.dog
