# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

sh = find_program('sh', required: false)
if not sh.found()
    subdir_done()
endif

foreach dir : [
    'basic',
    'no-match',
    'esc[pe',
    'root/sub',
]
    test(
        'editorconfig ' + dir,
        sh,
        args: [
            meson.current_source_dir() / 'test.sh',
            muon,
            meson.current_source_dir() / dir / 'meson.build'
        ],
        suite: 'fmt',
    )
endforeach
