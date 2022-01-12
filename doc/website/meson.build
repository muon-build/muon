mandoc = find_program('mandoc', required: get_option('docs'))

if not mandoc.found()
    subdir_done()
endif

website = []

website += custom_target(
    'muon.1.html',
    input: muon_1,
    output: 'muon.1.html',
    capture: true,
    command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', '@INPUT@'],
)

if not is_disabler(meson_reference_3)
    website += custom_target(
        'meson-reference.3.html',
        input: meson_reference_3,
        output: 'meson-reference.3.html',
        capture: true,
        command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', '@INPUT@'],
    )
endif

foreach f : ['index.html', 'man-style.css', 'muon_logo.svg']
    website += configure_file(input: files(f), output: f, copy: true)
endforeach
