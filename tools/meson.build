embedder = executable(
    'embedder',
    files(
        '../src/data/darr.c',
        '../src/error.c',
        '../src/log.c',
        'embedder.c',
    ),
    include_directories: include_dir,
    c_args: c_args,
    dependencies: platform_dep,
)
