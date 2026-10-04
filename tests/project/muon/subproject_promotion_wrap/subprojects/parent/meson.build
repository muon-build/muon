# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project('parent')

promoted = subproject('renamed')
assert(promoted.get_variable('location') == 'root')
assert(
    promoted.get_variable('build_dir') == meson.global_build_root() / 'subprojects/renamed-src',
)
