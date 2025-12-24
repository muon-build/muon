# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

# common platform sources
platform_sources = files(
    'assert.c',
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
        'null/backtrace.c',
    )
endif

if platform == 'posix'
    if host_machine.system() == 'darwin'
        platform_sources += files('null/rpath_fixer.c')
    else
        platform_sources += files('posix/rpath_fixer.c')
    endif

    native_backtrace = get_option('native_backtrace').enabled()
    if get_option('native_backtrace').auto()
        libdl = cc.find_library('dl', required: false)

        native_backtrace = (
            cc.has_function('dladdr', prefix: '#include <dlfcn.h>', args: ['-D_BSD_SOURCE'], dependencies: libdl)
            and cc.has_function('__builtin_frame_address')
            and cc.has_function('__builtin_return_address')
        )

        if native_backtrace and libdl.found()
            deps += libdl
        endif
    endif

    if native_backtrace
        muon_export_dynamic = true
        platform_sources += files('posix/backtrace.c')
    else
        platform_sources += files('null/backtrace.c')
    endif
endif
