# common platform sources
platform_sources = files(
    'filesystem.c',
    'mem.c',
    'path.c',
    'run_cmd.c',
    'uname.c',
)

foreach f : ['filesystem.c', 'path.c', 'rpath_fixer.c', 'term.c', 'uname.c']
    platform_sources += files(platform / f)
endforeach

platform_lib = static_library(
    'platform',
    platform_sources,
    include_directories: include_dir,
)

platform_dep = declare_dependency(link_with: platform_lib)
