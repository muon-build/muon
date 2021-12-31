mandoc = find_program('mandoc', required: get_option('docs'))

if not mandoc.found()
    subdir_done()
endif

custom_target(
    'muon.1.html',
    input: muon_1,
    output: 'muon.1.html',
    capture: true,
    command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', muon_1],
)

foreach f : ['index.html', 'man-style.css', 'muon_logo.svg']
    configure_file(input: files(f), output: f, copy: true)
endforeach
