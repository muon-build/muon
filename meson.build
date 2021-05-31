project(
	'boson',
	'c',
	version: '0.0.1',
	license: 'GPL3',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c11',
		'warning_level=3',
		'buildtype=debug',
	],
)

# compiler setup

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments([
	'-Wno-missing-braces',
	'-Wendif-labels',
	'-Wimplicit-fallthrough=2',
	'-Winit-self',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
	'-Wold-style-definition',
	'-Woverflow',
	'-Wpointer-arith',
	'-Wstrict-aliasing=2',
	'-Wstrict-prototypes',
	'-Wundef',
	'-fstrict-aliasing',
]), language: 'c')

# dependencies

deps = []
c_args = []

samu = subproject('samurai')
deps += [samu.get_variable('libsamu_dep')]

libcurl_dep = dependency('libcurl', required: false)
if libcurl_dep.found()
	deps += [libcurl_dep]
	c_args += ['-DBOSON_HAVE_CURL']
endif

deps += [dependency('zlib', required: true)]

# sources

src = files([
	'src/archive.c',
	'src/darr.c',
	'src/eval.c',
	'src/fetch.c',
	'src/filesystem.c',
	'src/functions/common.c',
	'src/functions/compiler.c',
	'src/functions/default.c',
	'src/functions/dependency.c',
	'src/functions/feature_opt.c',
	'src/functions/machine.c',
	'src/functions/meson.c',
	'src/functions/number.c',
	'src/functions/subproject.c',
	'src/hash.c',
	'src/inih.c',
	'src/interpreter.c',
	'src/lexer.c',
	'src/log.c',
	'src/main.c',
	'src/mem.c',
	'src/object.c',
	'src/output.c',
	'src/parser.c',
	'src/run_cmd.c',
	'src/workspace.c',
	'src/wrap.c',

	'src/external/microtar.c',
	'src/external/sha-256.c',
])

include_dir = include_directories('include')

executable(
	'boson',
	src,
	include_directories: include_dir,
	dependencies: deps,
	c_args: c_args,
)
