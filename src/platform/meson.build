# common platform sources
platform_sources = files(
    '../data/bucket_array.c',
    '../data/darr.c',
    '../data/hash.c',
    '../error.c',
    '../formats/ini.c',
    '../log.c',
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

platform_lib = static_library(
    'platform',
    platform_sources,
    include_directories: include_dir,
)

platform_dep = declare_dependency(link_with: platform_lib)
