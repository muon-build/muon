#!/bin/sh

for i in "${MESON_SOURCE_ROOT}/${MESON_SUBDIR:-.}/"*.c; do
  echo $i
done
