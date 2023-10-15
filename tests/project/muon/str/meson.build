# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('str')

foo = 'bar'

assert(foo.substring(64) == '')
assert(foo.substring(-64) == 'bar')
assert(foo.substring(0, 64) == 'bar')
assert(foo.substring(64, 0) == '')
