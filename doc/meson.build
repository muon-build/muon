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

summary('docs', true)
