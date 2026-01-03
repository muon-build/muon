# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

man_page_targets = {}

scdoc = find_program('scdoc', required: get_option('man-pages'))
if get_option('man-pages').disable_auto_if(not scdoc.found()).allowed()
    scdoc = find_program('scdoc')
    man_pages = {
        'muon': 1,
        'meson.build': 5,
    }

    foreach page, section : man_pages
        name = f'@page@.@section@'
        input = f'@name@.scd'
        tgt = custom_target(
            input,
            input: files(input),
            output: name,
            capture: true,
            feed: true,
            command: scdoc,
            install: true,
            install_dir: get_option('mandir') / 'man@0@'.format(section),
        )

        man_page_targets += {name: tgt}
    endforeach

endif

if not get_option('man-pages').disabled()
    custom_target(
        output: 'meson-reference.3',
        command: [muon, 'internal', 'dump_docs', '-oman'],
        capture: true,
        build_by_default: true,
        install: true,
        install_dir: get_option('mandir') / 'man3',
    )
endif

if get_option('website').enabled()
    subdir('website')
    subdir('book')

    custom_target(
        output: 'reference.html',
        command: [muon, 'internal', 'dump_docs', '-ohtml'],
        capture: true,
        build_by_default: true,
    )
endif

summary('docs', true)
