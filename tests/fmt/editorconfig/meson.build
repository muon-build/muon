# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

editorconfig_test = find_program('test.sh')

foreach dir : [
    'basic',
    'no-match',
    'esc[pe',
    'root/sub',
]
    test(
        'editorconfig ' + dir,
        editorconfig_test,
        args: [muon, meson.current_source_dir() / dir / 'meson.build'],
        suite: 'fmt',
    )
endforeach
