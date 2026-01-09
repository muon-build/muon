# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project(
    'muon-boost-tests',
    'cpp',
    version: '0.1.0',
    meson_version: '>=0.60.0',
)

# Test basic boost dependency detection (headers only)
boost_dep = dependency('boost', required: false)

if boost_dep.found()
    message('Boost found, version:', boost_dep.version())

    # Test header-only functionality
    test_header_exe = executable(
        'boost_header_test',
        'boost_header_test.cpp',
        dependencies: [boost_dep],
    )

    test(
        'boost_header_test',
        test_header_exe,
    )

    # Test with specific modules
    # Note: boost_system is header-only in modern Boost (1.69+), so we only test filesystem
    boost_filesystem_dep = dependency('boost', modules: ['filesystem'], required: false)

    if boost_filesystem_dep.found()
        test_filesystem_exe = executable(
            'boost_filesystem_test',
            'boost_filesystem_test.cpp',
            dependencies: [boost_filesystem_dep],
        )

        test(
            'boost_filesystem_test',
            test_filesystem_exe,
        )
    else
        message('Boost.Filesystem not found, skipping filesystem test')
    endif
else
    message('Boost not found on system, skipping boost tests')
endif
