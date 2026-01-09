# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project(
    'muon-threads-tests',
    'c',
    version: '0.1.0',
    meson_version: '>=0.60.0',
)

threads_dep = dependency('threads', required: true)

test_exe = executable(
    'threads_test',
    'threads_test.c',
    dependencies: [threads_dep],
)

test(
    'threads_test',
    test_exe,
)
