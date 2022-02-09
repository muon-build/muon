project('sub sub', 'c')

# different from meson: force b to be evaulated first, so it is found when a
# looks it up
subproject('b')

a = subproject('a')
lib = a.get_variable('l')

dependency(
    'not-found-dep',
    required: false,
    version: '>=1',
    fallback: ['c', 'notfound_dep'],
)

exe = executable('prog', 'prog.c', link_with: lib)
test('basic', exe)
