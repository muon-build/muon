project(
    'invalid C standard overridden to valid one',
    'c',
    # different than meson: meson doesn't validate default_options if they are
    # overridden on the commandline, while muon does.  This was a problem for
    # c_std in meson, since the option validation takes into account the
    # current compiler to restrict the possible choices.  In muon, all option
    # choices are static so we don't have this problem.
    # default_options: ['c_std=invalid99'],
    default_options: ['c_std=c99'],
)

exe = executable('main', 'main.c')
test('main', exe)
