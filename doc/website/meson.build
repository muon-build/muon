# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

mandoc = find_program('mandoc')

foreach name, tgt : man_page_targets
    name = f'@name@.html'
    custom_target(
        name,
        input: tgt,
        output: name,
        capture: true,
        command: [mandoc, '-T', 'html', '-O', 'style=man-style.css', '@INPUT@'],
        build_by_default: true,
    )
endforeach

python3 = find_program('python3', required: false)
if meson_proj.found() and python3.found()
    muon_signatures = custom_target(
        'muon_signatures.txt',
        output: 'muon_signatures.txt',
        command: [muon, 'internal', 'dump_funcs'],
        capture: true,
    )

    meson_signatures = meson_proj.get_variable('signatures')

    custom_target(
        'status.html',
        output: 'status.html',
        command: [
            'python3',
            files('signatures_to_html.py'),
            muon_signatures,
            meson_signatures,
        ],
        capture: true,
        build_by_default: true,
    )
endif

foreach f : [
    'index.html',
    'muon_ci.html',
    'man-style.css',
    'status.css',
    'main.css',
    'muon_logo.svg',
]
    configure_file(
        configuration: version_info,
        input: f,
        output: f,
    )
endforeach

summary('website', true)
