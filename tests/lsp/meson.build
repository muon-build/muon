# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

lsp_tests = [
    ['basic'],
    ['diagnostics'],
    ['diagnostics_with_edit'],
    ['many_edits'],
]

foreach t : lsp_tests
    name = t[0]
    test_name = name
    props = t.get(1, {})

    test_args = [
        'internal',
        'eval',
        meson.current_source_dir() / 'runner.meson',
        muon,
        meson.current_source_dir() / name,
        meson.current_build_dir() / name,
    ]

    test(
        test_name,
        muon,
        args: test_args,
        suite: ['lsp'],
    )
endforeach
