mandoc = find_program('mandoc', required: get_option('docs'))

if not mandoc.found()
    subdir_done()
endif

website = []

foreach name, tgt : man_page_targets
    name = f'@name@.html'
    website += custom_target(
        name,
        input: tgt,
        output: name,
        capture: true,
        command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', '@INPUT@'],
    )
endforeach

if not is_disabler(meson_reference_3)
    website += custom_target(
        'meson_reference.3.html',
        input: meson_reference_3,
        output: 'meson-reference.3.html',
        capture: true,
        command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', '@INPUT@'],
    )
endif

foreach f : ['index.html', 'man-style.css', 'muon_logo.svg']
    website += configure_file(
        configuration: version_info,
        input: files(f),
        output: f,
    )
endforeach
