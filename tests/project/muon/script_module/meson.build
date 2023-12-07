# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('test script_modules')

m = import('_test')

# test basic functions
assert(m.identity(32) == 32)

# test module-level globals
assert(not is_variable('global'))
assert(m.get_global() == [])
m.append_global(1)
m.append_global(2)
assert(m.get_global() == [1, 2])
assert(not is_variable('global'))
g = m.get_global()
g += 3
assert(m.get_global() == [1, 2])

# test function scopes
m.basic_scope()
assert(m.basic_scope() == 1)
assert(not is_variable('a'))

# test closures
f = m.factory(42)
assert(f() == 42)
assert(m.factory(f)()() == 42)
h = m.incrementer()
assert(h() == 0)
assert(h() == 1)
assert(h() == 2)

# test wrapping meson functions
cat = find_program('cat')
copy = m.custom_target_wrap(
    {
        'env': {'custom_var': '1'},
        'command': [cat],
        'feed': true,
        'capture': true,
        'build_by_default': true,
    },
)
copy('f', {'input': 'meson.build', 'output': 'copied_meson.build'})

stat = find_program('stat')
test_exists = m.test_wrap(stat, {'suite': 'test_exists'})
test_exists('t1', ['copied_meson.build'])

# make sure keywords dont leak
func = 1
return = 2
endfunc = 3
message([func, return, endfunc])
