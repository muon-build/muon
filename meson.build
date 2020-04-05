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

add_project_arguments(
	'-Wno-unused-parameter',
	language: 'c'
)

src = files(
	'main.c',
)

executable(
	'boson',
	src,
	include_directories: [include_dir],
	install: true
)

