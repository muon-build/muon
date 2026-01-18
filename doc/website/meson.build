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

foreach f : [
    'index.html',
    'man-style.css',
    'muon_logo.svg',
    'style.css',
]
    configure_file(
        configuration: version_info,
        input: f,
        output: f,
    )
endforeach

subdir('ci')

summary('website', true)
