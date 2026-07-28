# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('str')

foo = 'bar'

assert(foo.substring(64) == '')
assert(foo.substring(-64) == 'bar')
assert(foo.substring(0, 64) == 'bar')
assert(foo.substring(64, 0) == '')

choices = ['foo', 'buz']

assert('buz' in choices)
assert(not ('bug' in choices))

# enum-like combo option strings
buildtype = get_option('buildtype')
sanitize = get_option('b_sanitize')

# No errors or warnings in analyze
assert(('address' == sanitize) in [true, false])
assert(('address' in sanitize) in [true, false])
assert(('debug' == buildtype) in [true, false])
assert(('debug' in buildtype) in [true, false])
assert(('release' == buildtype) in [true, false])
assert(('release' in buildtype) in [true, false])

# These combo option comparisons will issue warnings in analyze
# but will run without error or warning in setup/build
assert(('nope' == sanitize) in [true, false])
assert(('nope' in sanitize) in [true, false])
assert(('lease' == buildtype) in [true, false])
assert(('lease' in buildtype) in [true, false])
assert(('bug' == buildtype) in [true, false])
assert(('bug' in buildtype) in [true, false])
