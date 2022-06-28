project('gamma project', 'c', subproject_dir: 'contrib/subprojects')

# different than meson: nested subproject promotion
subproject('beta')
a = subproject('alpha')
lib = a.get_variable('l')

# Ensure that the dependency version is not checked for a not-found dependency
notfound = dependency(
    '',
    version: '>=1.0',
    required: false,
    fallback: ['beta', 'notfound'],
)

exe = executable('prog', 'prog.c', link_with: lib)
test('basic', exe)
