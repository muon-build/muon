# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

# common platform sources
platform_sources = files(
    'filesystem.c',
    'mem.c',
    'os.c',
    'path.c',
    'run_cmd.c',
    'uname.c',
)

foreach f : [
    'filesystem.c',
    'init.c',
    'log.c',
    'os.c',
    'path.c',
    'run_cmd.c',
    'term.c',
    'timer.c',
    'uname.c',
]
    platform_sources += files(platform / f)
endforeach

if host_machine.system() == 'windows'
    include_dir += include_directories('../../include/platform/windows')
    platform_sources += files(
        'windows/rpath_fixer.c',
        'windows/win32_error.c',
    )
endif

if platform == 'posix'
    if host_machine.system() == 'darwin'
        platform_sources += files('null/rpath_fixer.c')
    else
        platform_sources += files('posix/rpath_fixer.c')
    endif

    # getloadavg detection

    if host_machine.system() == 'darwin'
        defines = []
    else
        defines = ['-D_POSIX_C_SOURCE=200809L', '-D_BSD_SOURCE']
    endif

    if cc.has_function(
        'getloadavg',
        args: defines,
        prefix: '#include <stdlib.h>',
    )
        c_args += '-DMUON_HAVE_GETLOADAVG'
    endif
endif
