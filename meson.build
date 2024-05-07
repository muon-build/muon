# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
# SPDX-License-Identifier: GPL-3.0-only

project(
    'muon',
    'c',
    version: '0.2.0',
    license: 'GPL-3.0-only',
    meson_version: '>=0.59.0',
    default_options: [
        'c_std=c99',
        'warning_level=3',
        'buildtype=debug',
        # 'optimization=0',
        'default_library=static',
    ],
)

fs = import('fs')

# version information

git = find_program('git', required: false)
if git.found() and fs.is_dir('.git')
    rev = run_command(git, 'rev-parse', '--short', 'HEAD', check: true)
    git_sha = rev.stdout().strip()
else
    git_sha = ''
endif

version_info = configuration_data()
version_info.set('version', meson.project_version())
version_info.set('vcs_tag', git_sha)
version_info.set('meson_compat', '1.1')

# platform

platform = host_machine.system()
if (
    platform in [
        'android',
        'cygwin',
        'darwin',
        'freebsd',
        'linux',
        'msys2',
        'netbsd',
        'openbsd',
        'sunos',
        'emscripten',
    ]
)
    platform = 'posix'
elif platform not in ['windows']
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

if cc.get_id() == 'msvc'
    add_project_arguments(
        cc.get_supported_arguments(
            [
                '/wd4027', # -Wstrict-prototypes
                '/wd4056', # -Woverflow
            ],
        ),
        language: 'c',
    )
else
    add_project_arguments(
        cc.get_supported_arguments(
            [
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
                '-Wstrict-aliasing=2',
                '-Wstrict-prototypes',
                '-Wundef',
                '-Wvla',
                '-fstrict-aliasing',
            ],
        ),
        language: 'c',
    )
endif

add_project_arguments('-DMUON_BOOTSTRAPPED', language: 'c')

include_dir = [include_directories('include')]

subdir('tools')
subdir('src')

# tracy
tracy_dep = dependency('tracy', required: get_option('tracy'))
if tracy_dep.found()
    add_languages('cpp')
    c_args += ['-DTRACY_ENABLE']
    deps += tracy_dep
endif

muon = executable(
    'muon',
    src,
    dependencies: deps,
    include_directories: include_dir,
    link_args: link_args,
    c_args: c_args,
    cpp_args: c_args,
    install: true,
)

python3 = find_program('python3', required: false)

subdir('tests')
subdir('doc')
