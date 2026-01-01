# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('foo', module_dir: '.')

m = import('foo')
assert(m.baz() == 42)
meson.export_module('m', m)
