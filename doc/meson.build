# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

docs_opt = get_option('docs').require(
    meson.can_run_host_binaries(),
    error_message: 'host binaries must be executable to build docs',
).enable_auto_if(meson.can_run_host_binaries())

man_pages_opt = get_option('man-pages').require(docs_opt.enabled())
website_opt = get_option('website').require(docs_opt.enabled())

summary('docs', docs_opt.allowed())

if not docs_opt.allowed()
    subdir_done()
endif

man_page_targets = {}

if man_pages_opt.allowed()
    foreach page : [
        ['muon', 1, ['-c']],
        ['meson-reference', 3, []],
    ]
        name = page[0]
        section = page[1]
        name += f'.@section@'
        flags = page[2]
        man_page_targets += {
            name: custom_target(
                output: name,
                command: [muon, 'internal', 'dump_docs', '-oman'] + flags,
                capture: true,
                build_by_default: true,
                install: true,
                install_dir: get_option('mandir') / f'man@section@',
            ),
        }
    endforeach

    scdoc = find_program('scdoc', required: get_option('man-pages'))
    if scdoc.found()
        name = 'meson.build.5'
        man_page_targets += {
            name: custom_target(
                output: name,
                input: files('meson.build.5.scd'),
                capture: true,
                feed: true,
                command: scdoc,
                install: true,
                install_dir: get_option('mandir') / 'man5',
            ),
        }
    endif
endif

if website_opt.allowed()
    subdir('website')
    subdir('book')

    custom_target(
        output: 'reference.html',
        command: [muon, 'internal', 'dump_docs', '-ohtml'],
        capture: true,
        build_by_default: true,
    )
endif
