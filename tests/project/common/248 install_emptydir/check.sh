#!/bin/sh
set -eux
test -d "$DESTDIR"/usr/share/new_directory
test -d "$DESTDIR"/usr/share/new_directory/subdir
