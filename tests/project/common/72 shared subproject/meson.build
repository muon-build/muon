project('A', 'c')

# different than meson: no auto-population of subprojects
C = subproject('C')
c = C.get_variable('c')

B = subproject('B')
b = B.get_variable('b')

a = executable('a', 'a.c', link_with: [b, c])
test('a test', a)
