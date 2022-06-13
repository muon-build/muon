project('A', 'c', subproject_dir: 'custom_subproject_dir')

# different than meson: subproject promotion.  Therefore, subproject('C') first
# so B can find it.
C = subproject('C')
c = C.get_variable('c')

B = subproject('B')
b = B.get_variable('b')

subdir('other_subdir')

a = executable('a', 'a.c', link_with: [b, c])
test('a test', a)
