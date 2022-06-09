#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/progname/C/datafile.dat
test -f "$DESTDIR"/usr/share/progname/es/datafile.dat
test -f "$DESTDIR"/usr/share/progname/fr/rename_datafile.dat
