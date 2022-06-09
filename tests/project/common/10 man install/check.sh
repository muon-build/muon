#!/bin/sh
set -eux
test -f "$DESTDIR"/usr/share/man/man1/foo.1
test -f "$DESTDIR"/usr/share/man/fr/man1/foo.1
test -f "$DESTDIR"/usr/share/man/man2/bar.2
test -f "$DESTDIR"/usr/share/man/man1/vanishing.1
test -f "$DESTDIR"/usr/share/man/man2/vanishing.2
test -f "$DESTDIR"/usr/share/man/man1/baz.1
