project(
	'boson',
	'c',
	version: '0.0.1',
	license: 'GPL3',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c99',
		'warning_level=3',
		'werror=true',
		'buildtype=debugoptimized',
	],
)

#-DVERSION='"$(VERSION)"'
add_project_arguments(
	'-DVERSION="@0@"'.format(meson.project_version()),
	'-Wno-unused-parameter',
	language: 'c',
)

src = files(
	'src/getopt_long.c',
	'src/log.c',
	'src/ninja.c',
	'src/token.c',
	'src/lexer.c',
	'src/parse.c',
	'src/setup.c',
	'src/main.c',
)

include_dir = include_directories('include')

executable(
	'boson',
	src,
	include_directories: [include_dir],
	install: true,
)
