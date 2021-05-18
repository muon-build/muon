project(
	'boson',
	'c',
	version: '0.0.1',
	license: 'GPL3',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c11',
		'warning_level=3',
		'buildtype=debugoptimized',
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

src = files([
	'src/builtin.c',
	'src/darr.c',
	'src/filesystem.c',
	'src/hash.c',
	'src/interpreter.c',
	'src/lexer.c',
	'src/log.c',
	'src/main.c',
	'src/mem.c',
	'src/object.c',
	'src/output.c',
	'src/parser.c',
	'src/workspace.c',
])

include_dir = include_directories('include')

samu = subproject('samurai')
libsamu = samu.get_variable('libsamu_dep')

executable(
	'boson',
	src,
	include_directories: include_dir,
	dependencies: [libsamu],
)
