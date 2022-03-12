project('A', 'c', subproject_dir: 'custom_subproject_dir')

# different than meson: no subproject promotion
C = subproject('C')
c = C.get_variable('c')

B = subproject('B')
b = B.get_variable('b')

a = executable('a', 'a.c', link_with: [b, c])
test('a test', a)
