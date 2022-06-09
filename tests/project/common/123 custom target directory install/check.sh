#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/doc/testpkgname/html/a.html
test -f "$DESTDIR"/usr/share/doc/testpkgname/html/b.html
test -f "$DESTDIR"/usr/share/doc/testpkgname/html/c.html
