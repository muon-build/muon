#!/bin/sh

# requirements:
# - cc
# - pkgconf
# - libpkgconf

set -eu

dir="$1"
mkdir -p "$dir"

cat \
	src/args.c \
	src/backend/ninja.c \
	src/coerce.c \
	src/compilers.c \
	src/data/bucket_array.c \
	src/data/darr.c \
	src/data/hash.c \
	src/error.c \
	src/external/curl_null.c \
	src/external/libpkgconf.c \
	src/external/samu_null.c \
	src/external/zlib_null.c \
	src/formats/ini.c \
	src/formats/tar.c \
	src/functions/array.c \
	src/functions/boolean.c \
	src/functions/build_target.c \
	src/functions/common.c \
	src/functions/compiler.c \
	src/functions/configuration_data.c \
	src/functions/custom_target.c \
	src/functions/default.c \
	src/functions/default/configure_file.c \
	src/functions/default/custom_target.c \
	src/functions/default/dependency.c \
	src/functions/default/options.c \
	src/functions/default/setup.c \
	src/functions/dependency.c \
	src/functions/dict.c \
	src/functions/environment.c \
	src/functions/external_library.c \
	src/functions/external_program.c \
	src/functions/feature_opt.c \
	src/functions/file.c \
	src/functions/machine.c \
	src/functions/meson.c \
	src/functions/modules.c \
	src/functions/modules/fs.c \
	src/functions/modules/pkgconfig.c \
	src/functions/modules/python.c \
	src/functions/number.c \
	src/functions/run_result.c \
	src/functions/string.c \
	src/functions/subproject.c \
	src/install.c \
	src/lang/eval.c \
	src/lang/interpreter.c \
	src/lang/lexer.c \
	src/lang/object.c \
	src/lang/parser.c \
	src/lang/serial.c \
	src/lang/workspace.c \
	src/log.c \
	src/machine_file.c \
	src/main.c \
	src/opts.c \
	src/platform/dirs.c \
	src/platform/filesystem.c \
	src/platform/mem.c \
	src/platform/path.c \
	src/platform/run_cmd.c \
	src/sha_256.c \
	src/tests.c \
	src/version.c.in \
	src/wrap.c \
	> "$dir/muon.c"

${CC:-cc} ${CFLAGS:-} -g -Iinclude $(pkgconf --cflags libpkgconf) -o "$dir/muon.o" -c "$dir/muon.c"

${CC:-cc} ${LDFLAGS:-} "$dir/muon.o" $(pkgconf --libs libpkgconf) -o "$dir/muon"
