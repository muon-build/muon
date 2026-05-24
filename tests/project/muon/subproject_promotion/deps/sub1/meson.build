# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project('sub1', subproject_dir: 'nested_deps')

promoted = subproject('sub2')
assert(promoted.get_variable('location') == 'root')
assert(
    promoted.get_variable('build_dir') == meson.global_build_root() / 'deps/sub2',
)

nested = subproject('nested')
assert(nested.get_variable('location') == 'nested')
assert(
    nested.get_variable('build_dir') == meson.current_build_dir() / 'nested_deps/nested',
)

assert(not subproject('missing', required: false).found())
