#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/progname/datafile.dat
test -f "$DESTDIR"/usr/share/progname/fileobject_datafile.dat
test -f "$DESTDIR"/usr/share/progname/vanishing.dat
test -f "$DESTDIR"/usr/share/progname/vanishing2.dat
test -f "$DESTDIR"/'usr/share/data install test/renamed file.txt'
test -f "$DESTDIR"/'usr/share/data install test/somefile.txt'
test -f "$DESTDIR"/'usr/share/data install test/some/nested/path.txt'
test -f "$DESTDIR"/'usr/share/renamed/renamed 2.txt'
test -f "$DESTDIR"/'usr/share/renamed/renamed 3.txt'
test -f "$DESTDIR"/etc/etcfile.dat
test -f "$DESTDIR"/usr/bin/runscript.sh
