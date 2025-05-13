# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
# SPDX-License-Identifier: GPL-3.0-only

project(
    'muon',
    'c',
    version: '0.5.0',
    license: 'GPL-3.0-only',
    meson_version: '>=1.3.0',
    default_options: {
        'warning_level': '3',
        'buildtype': 'debug',
        'default_library': 'static',
        'cpp_std': 'c++11',
        'force_fallback_for': [
            'glad',
            'glfw3',
            'imgui',
        ],
    },
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
version_info.set('meson_compat', '1.7')

configure_file(
    configuration: version_info,
    input: 'tools/ci/version.sh.in',
    output: 'version.sh',
)

# platform

platform = host_machine.system()
if platform != 'windows'
    # Assume everything that is not windows is posix.  This will likely need to
    # change in the future.
    platform = 'posix'
endif

# compiler setup

c_args = [
    '-DMUON_PLATFORM_' + platform,
    '-DMUON_ENDIAN=@0@'.format({'big': 1, 'little': 0}[host_machine.endian()]),
]
link_args = []

if get_option('static')
    c_args += '-DMUON_STATIC'
    link_args += '-static'
endif

cc = meson.get_compiler('c')

if cc.get_argument_syntax() == 'msvc'
    add_project_arguments(
        cc.get_supported_arguments(
            [
                '/we4027', # -Wstrict-prototypes
                '/we4056', # -Woverflow
                '/we4013', # function undefined; assuming extern returning int

                '/wd4100', # -Wno-unused-parameter
                '/wd4706', # assignment within conditional expression
                '/wd4267', # conversion from x to y, possible loss of data
                '/wd4244', # conversion from x to y, possible loss of data
                '/wd4456', # declaration of identifier hides previous local declaration
                '/wd4457', # declaration of 'identifier' hides function parameter

                # Lots of false positivies due to not understanding UNREACHABLE
                '/wd4701', # potentially uninitialized local variable name used
                '/wd4703', # potentially uninitialized local variable name used
                '/wd4702', # unreachable code

                # msvc complaining about flexible array member
                '/wd4200', # nonstandard extension used: zero-sized array in struct/union

                '/std:c11',
                # Occurs spuriously due to /std:c11 enabling a
                # standards-conformant preprocessor
                '/wd5105', # macro expansion producing 'defined' has undefined behavior
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
                '-std=c99',
            ],
        ),
        language: 'c',
    )
endif

if platform == 'windows'
    add_project_arguments(
        ['-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_DEPRECATE'],
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

# meson-docs project
python3 = find_program('python3', required: get_option('meson-docs'))

if python3.found() and not get_option('meson-docs').disabled()
    meson_docs_proj = subproject('meson-docs', required: get_option('meson-docs'))
    if meson_docs_proj.found()
        src += meson_docs_proj.get_variable('meson_docs_h')
        c_args += ['-DHAVE_MESON_DOCS_H']
    endif
else
    meson_docs_proj = dependency('', required: false)
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

subdir('tests')
subdir('doc')
