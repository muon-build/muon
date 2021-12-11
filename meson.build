project(
    'muon',
    'c',
    version: '0.0.1',
    license: 'GPL3',
    meson_version: '>=0.53.0',
    default_options: [
        'c_std=c99',
        'warning_level=3',
        'buildtype=debugoptimized',
        'default_library=static',
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

# compiler setup

cc = meson.get_compiler('c')
add_project_arguments(
    cc.get_supported_arguments(
        [
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
            '-DMUON_BOOTSTRAPPED',
        ],
    ),
    language: 'c',
)

deps = []
c_args = []

include_dir = include_directories('include')

subdir('tools')
subdir('src')

muon = executable(
    'muon',
    src,
    include_directories: include_dir,
    dependencies: deps,
    c_args: c_args,
    install: true,
)

subdir('tests')
