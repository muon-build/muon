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

if meson_docs_proj.found()
    muon_signatures = custom_target(
        'muon_signatures.txt',
        output: 'muon_signatures.txt',
        command: [muon, 'internal', 'dump_funcs'],
        capture: true,
    )

    meson_signatures = meson_docs_proj.get_variable('signatures')

    configure_file(
        configuration: version_info,
        input: 'version_info.py.in',
        output: 'version_info.py',
    )

    env = environment()
    env.append('PYTHONPATH', meson.current_build_dir())

    custom_target(
        'status.html',
        output: 'status.html',
        command: [
            python3,
            files('signatures_to_html.py'),
            muon_signatures,
            meson_signatures,
        ],
        capture: true,
        build_by_default: true,
        env: env,
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
