#!/bin/sh

# requirements:
# - c toolchain (gcc tested)
# - ninja compatible build tool
# - pkgconf
# - libpkgconf

set -eu

dir="$1"

cat     src/coerce.c \
	src/darr.c \
	src/eval.c \
	src/external/curl_null.c \
	src/external/libpkgconf.c \
	src/external/microtar.c \
	src/external/samu_null.c \
	src/external/sha-256.c \
	src/external/zlib_null.c \
	src/filesystem.c \
	src/functions/common.c \
	src/functions/compiler.c \
	src/functions/configuration_data.c \
	src/functions/default.c \
	src/functions/default/configure_file.c \
	src/functions/default/dependency.c \
	src/functions/default/options.c \
	src/functions/dependency.c \
	src/functions/dict.c \
	src/functions/external_library.c \
	src/functions/external_program.c \
	src/functions/feature_opt.c \
	src/functions/machine.c \
	src/functions/meson.c \
	src/functions/number.c \
	src/functions/run_result.c \
	src/functions/string.c \
	src/functions/subproject.c \
	src/hash.c \
	src/inih.c \
	src/interpreter.c \
	src/lexer.c \
	src/log.c \
	src/main.c \
	src/mem.c \
	src/object.c \
	src/opts.c \
	src/output.c \
	src/parser.c \
	src/run_cmd.c \
	src/tests.c \
	src/version.c.in \
	src/workspace.c \
	src/wrap.c \
	| cc -Iinclude $(pkgconf --cflags libpkgconf) -x c -o muon.o -c -

cc muon.o $(pkgconf --libs libpkgconf) -o muon

./muon setup -Dsamu=disabled stage1
ninja -C stage1
rm muon.o muon

stage1/muon setup stage2
ninja -C stage2
