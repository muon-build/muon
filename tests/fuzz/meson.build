# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

afl_fuzz = find_program('afl-fuzz', required: false)

if not (cc.cmd_array()[0].startswith('afl-clang') and afl_fuzz.found())
    subdir_done()
endif

run_target(
    'fuzz-lang',
    command: [
        afl_fuzz,
        '-x', meson.current_source_dir() / 'meson.dict',
        '-i', meson.current_source_dir() / 'lang',
        '-o', meson.current_build_dir() / 'lang',
        '--', muon,
        'internal',
        'eval',
        '-s',
        '-',
    ],
)

# Fuzzing serial_load has a low signal to noise ratio, since serial_load
# doesn't make any guarantees about de-serializing untrusted input (much like
# python's pickle).  However, some of the crashes that afl found have been
# useful, so I'm including the run target here anyway.

subdir('dump-input')

run_target(
    'fuzz-dump',
    command: [
        afl_fuzz,
        '-i', fuzz_dump_input,
        '-o', meson.current_build_dir() / 'dump',
        '--', muon,
        'internal',
        'eval',
        meson.current_source_dir() / 'load_dump.meson',
    ],
    depends: [fuzz_dump_input_dat],
)
