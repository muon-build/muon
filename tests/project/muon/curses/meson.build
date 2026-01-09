# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project(
    'muon-curses-tests',
    'c',
    version: '0.1.0',
    meson_version: '>=0.60.0',
)

curses_dep = dependency('curses', required: true)

test_exe = executable(
    'curses_test',
    'curses_test.c',
    dependencies: [curses_dep],
)

test(
    'curses_test',
    test_exe,
)
