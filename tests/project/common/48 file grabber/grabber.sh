#!/bin/sh

# different from meson: run_command() runs from the project root
for i in "${MESON_SOURCE_ROOT}/${MESON_SUBDIR:-.}/"*.c; do
  echo $i
done
