project(
    'muon',
    'c',
    version: '0.0.1',
    license: 'GPL3',
    meson_version: '>=0.59.0',
    default_options: [
        'c_std=c99',
        'warning_level=3',
        'buildtype=debugoptimized',
        'default_library=static',
    ],
)

fs = import('fs')

# version information

git = find_program('git', required: false)
if git.found() and fs.is_dir('.git')
    git_rev_parse = run_command(git, 'rev-parse', '--short', '@', check: true)
    git_sha = git_rev_parse.stdout().strip()
else
    git_sha = ''
endif

version_info = configuration_data()
version_info.set('version', meson.project_version())
version_info.set('vcs_tag', git_sha)

# platform

platform = host_machine.system()
if platform in ['freebsd', 'linux', 'netbsd', 'openbsd', 'sunos']
    platform = 'posix'
else
    warning(
        'configuring muon for an unsupported platform "@0@"'.format(platform),
    )
    platform = 'null'
endif

# compiler setup

c_args = ['-DMUON_PLATFORM_' + platform]
link_args = []

if get_option('static')
    c_args += '-DMUON_STATIC'
    link_args += '-static'
endif

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
            '-Wno-missing-field-initializers',
            '-Wno-unused-parameter',
            '-Wold-style-definition',
            '-Woverflow',
            '-Wstrict-aliasing=2',
            '-Wstrict-prototypes',
            '-Wundef',
            '-fstrict-aliasing',
        ],
    ),
    language: 'c',
)

add_project_arguments('-DMUON_BOOTSTRAPPED', language: 'c')

include_dir = include_directories('include')

# platform dep required for tools
subdir('src/platform')

subdir('tools')
subdir('src')

muon = executable(
    'muon',
    src,
    dependencies: deps,
    include_directories: include_dir,
    c_args: c_args,
    link_args: link_args,
    install: true,
)

subdir('tests')
subdir('doc')
