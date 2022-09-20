# This is a hack to make the dump input be in a directory by itself.
#
fuzz_dump_input_dat = custom_target(
    'fuzz-dump-input',
    output: 'fuzz-dump-input.dat',
    command: [
        muon,
        'internal',
        'eval',
        meson.current_source_dir() / 'gen_dump.meson',
        '@OUTPUT@',
    ],
)

fuzz_dump_input = meson.current_build_dir()
