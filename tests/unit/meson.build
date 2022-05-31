tests = [
    ['path', [files('path.c')], {'dependencies': muon_dep}],
]

foreach t : tests
    exe = executable(
        t[0],
        t[1],
        include_directories: include_dir,
        c_args: c_args,
        kwargs: t[2],
    )
    test(t[0], exe, suite: 'unit')
endforeach
