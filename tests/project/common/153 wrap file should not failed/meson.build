project(
    'mainproj',
    'c',
    default_options: ['wrap_mode=nodownload'],
)

subproject('zlib')
foo = subproject('foo', required: false)
if not foo.found()
    error('MESON_SKIP_TEST: libarchive support is not enabled')
endif

bar = subproject('bar')

libfoo = foo.get_variable('libfoo')
libbar = bar.get_variable('libbar')

executable('grabprog', files('src/subprojects/prog.c'))
executable('grabprog2', files('src/subprojects/foo/prog2.c'))
subdir('src')

subproject('patchdir')
