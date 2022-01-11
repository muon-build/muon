mandoc = find_program('mandoc', required: get_option('docs'))
tar = find_program('tar', required: get_option('docs'))
gzip = find_program('gzip', required: get_option('docs'))

if not (mandoc.found() and tar.found() and gzip.found())
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

website += custom_target(
    'meson-reference.3.html',
    input: meson_reference_3,
    output: 'meson-reference.3.html',
    capture: true,
    command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', '@INPUT@'],
)

foreach f : ['index.html', 'man-style.css', 'muon_logo.svg']
    website += configure_file(input: files(f), output: f, copy: true)
endforeach

basenames = []
foreach f : website
    basenames += f.full_path().split('/')[-1]
endforeach

website_tar = custom_target(
    'website.tar',
    input: website,
    output: 'website.tar',
    command: [tar, 'c', '-C', meson.current_build_dir(), '-f', '@OUTPUT@'] + basenames,
)

custom_target(
    'website.tar.gz',
    input: website_tar,
    output: 'website.tar.gz',
    command: [gzip, '-k', '-f', '@INPUT@'],
)
