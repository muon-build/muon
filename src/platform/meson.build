# common platform sources
platform_sources = files(
    'filesystem.c',
    'mem.c',
    'path.c',
    'run_cmd.c',
    'term.c',
    'uname.c',
)

foreach f : ['rpath_fixer.c']
    platform_sources += files(platform / f)
endforeach
