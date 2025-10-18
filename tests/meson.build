# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

add_test_setup('valgrind', exclude_suites: 'project', exe_wrapper: ['valgrind'])
add_test_setup('no_python', exclude_suites: 'requires_python')

subdir('fmt')
subdir('fuzz')
subdir('lang')
subdir('lsp')
subdir('project')

foreach script : embedded_scripts
    if not script.endswith('.meson')
        continue
    elif script.startswith('options/')
        continue
    endif

    mode = 'module'
    if script.startswith('commands/')
        mode = 'script'
    endif

    test(
        f'@script@',
        muon,
        args: [
            'analyze',
            '-Werror',
            '-m', mode,
            'file',
            embedded_scripts_base / script,
        ],
        suite: 'analyze',
    )
endforeach
