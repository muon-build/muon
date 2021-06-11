project(
	'muon',
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


# version information

git = find_program('git', required: false)
if git.found()
	git_rev_parse = run_command(git, 'rev-parse', '--short', '@')
	git_sha = git_rev_parse.stdout().strip()
else
	git_sha = 'unknown'
endif

version_info = configuration_data()
version_info.set('version', meson.project_version())
version_info.set('vcs_tag', git_sha)

message(git_sha) # TODO use this

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

deps = [
	dependency('libpkgconf', required: true),
]
c_args = []

if get_option('samu')
	samu = subproject('samurai')
	deps += [samu.get_variable('libsamu_dep')]
	c_args += ['-DMUON_HAVE_SAMU']
endif

libcurl_dep = dependency('libcurl', required: get_option('curl'))
if libcurl_dep.found()
	deps += [libcurl_dep]
	c_args += ['-DMUON_HAVE_CURL']
endif

zlib_dep = dependency('zlib', required: get_option('zlib'))
if zlib_dep.found()
	deps += [zlib_dep]
	c_args += ['-DMUON_HAVE_ZLIB']
endif

include_dir = include_directories('include')

subdir('src')

executable(
	'muon',
	src,
	include_directories: include_dir,
	dependencies: deps,
	c_args: c_args,
)
