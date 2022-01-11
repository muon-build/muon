sh = find_program('sh')
scdoc = find_program('scdoc', required: get_option('docs'))

if not scdoc.found()
    subdir_done()
endif

muon_1 = custom_target(
    'muon.1.scd',
    input: files('muon.1.scd'),
    output: 'muon.1',
    capture: true,
    command: [sh, '-c', '@0@ < @INPUT@'.format(scdoc.path())],
    install: true,
    install_dir: get_option('mandir') / 'man1',
)

meson_proj = subproject('meson-docs', required: false)
if meson_proj.found()
    meson_reference_3 = meson_proj.get_variable('meson_reference_3')
else
    warning('meson-docs not found, meson-reference.3 will not be build')
    meson_reference_3 = disabler()
endif

subdir('website')

summary('docs', true)
