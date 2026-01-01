# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('test script_modules', module_dir: '.')

m = import('test_module')

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
cat = find_program('cat', required: false)
if cat.found()
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
endif

stat = find_program('stat', required: false)
if stat.found()
    test_exists = m.test_wrap(stat, {'suite': 'test_exists'})
    test_exists('t1', ['copied_meson.build'])
endif

# make sure keywords dont leak
func = 1
return = 2
endfunc = 3
message([func, return, endfunc])

# make sure modules can access global objects
assert(m.access_globals() == meson.project_name())

# make sure lists dont get flattened
assert(m.array_kwarg_default() == [])
assert(m.array_kwarg_default(bar: ['meow']) == ['meow'])

# test type of
assert(m.get_type('asdf') == 'str')
assert(m.get_type([]) == 'list')
assert(m.get_type({}) == 'dict')

# test listify
assert(m.listify_arg('meow') == ['meow'])
assert(m.listify_arg(['meow']) == ['meow'])
assert(m.listify_arg([[['meow']]]) == ['meow'])

# test glob
assert(m.glob_arg(1, 2, 3, 4, 5) == [1, 2, 3, 4, 5])

# test nested types
m.typed_list(['1', '2'])

# this is mostly to test the analyzer
assert(m.multi_return('a') == 1)
assert(m.multi_return('b') == 2)
assert(m.multi_return('c') == 0)

# test internal calls not changing interpreter state.
m.returns_null()

# test meson.project()
assert('c' not in m.project()['toolchains']['host'])
add_languages('c')
assert('c' in m.project()['toolchains']['host'])

# subprojects with modules
sub = subproject('foo')
assert(sub.import('m').baz() == 42)
assert(sub.import('bar', required: false).found() == false)
