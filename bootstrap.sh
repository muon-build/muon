#!/bin/sh

# Requirements:
# - c99
# - cat
# - mkdir
# - sh
# Optional requirements:
# - pkgconf
# - libpkgconf

set -eux

dir="$1"
mkdir -p "$dir"

if command -v pkgconf >/dev/null && pkgconf libpkgconf; then
	pkgconf_src="libpkgconf.c"
	pkgconf_cflags="$(pkgconf --cflags libpkgconf)"
	pkgconf_libs="$(pkgconf --libs libpkgconf)"
else
	pkgconf_src="libpkgconf_null.c"
	pkgconf_cflags=""
	pkgconf_libs=""
fi

cat \
	src/args.c \
	src/backend/backend.c \
	src/backend/common_args.c \
	src/backend/ninja.c \
	src/backend/ninja/alias_target.c \
	src/backend/ninja/build_target.c \
	src/backend/ninja/custom_target.c \
	src/backend/ninja/rules.c \
	src/backend/output.c \
	src/coerce.c \
	src/compilers.c \
	src/data/bucket_array.c \
	src/data/darr.c \
	src/data/hash.c \
	src/embedded.c \
	src/error.c \
	src/external/$pkgconf_src \
	src/external/libarchive_null.c \
	src/external/libcurl_null.c \
	src/external/samurai_null.c \
	src/formats/ini.c \
	src/functions/array.c \
	src/functions/boolean.c \
	src/functions/both_libs.c \
	src/functions/build_target.c \
	src/functions/common.c \
	src/functions/compiler.c \
	src/functions/configuration_data.c \
	src/functions/custom_target.c \
	src/functions/default.c \
	src/functions/default/build_target.c \
	src/functions/default/configure_file.c \
	src/functions/default/custom_target.c \
	src/functions/default/dependency.c \
	src/functions/default/options.c \
	src/functions/default/subproject.c \
	src/functions/dependency.c \
	src/functions/dict.c \
	src/functions/disabler.c \
	src/functions/environment.c \
	src/functions/external_program.c \
	src/functions/feature_opt.c \
	src/functions/file.c \
	src/functions/generator.c \
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
	src/guess.c \
	src/install.c \
	src/lang/analyze.c \
	src/lang/eval.c \
	src/lang/fmt.c \
	src/lang/interpreter.c \
	src/lang/lexer.c \
	src/lang/object.c \
	src/lang/parser.c \
	src/lang/serial.c \
	src/lang/string.c \
	src/lang/workspace.c \
	src/log.c \
	src/machine_file.c \
	src/main.c \
	src/options.c \
	src/opts.c \
	src/platform/filesystem.c \
	src/platform/mem.c \
	src/platform/null/rpath_fixer.c \
	src/platform/path.c \
	src/platform/run_cmd.c \
	src/platform/term.c \
	src/platform/uname.c \
	src/rpmvercmp.c \
	src/sha_256.c \
	src/tests.c \
	src/version.c.in \
	src/wrap.c \
	> "$dir/muon.c"

${CC:-c99} -g -Iinclude $pkgconf_cflags -o "$dir/muon.o" -c "$dir/muon.c"
${CC:-c99} -o "$dir/muon" "$dir/muon.o" $pkgconf_libs
