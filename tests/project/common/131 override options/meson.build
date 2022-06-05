project(
    'option override',
    'c',
    default_options: ['b_ndebug=false', 'debug=true'],
)

a = executable('a', 'needs_no_b_ndebug.c')
b = executable('b', 'needs_b_ndebug.c', override_options: ['b_ndebug=true'])
test('a', a)
test('b', b)
