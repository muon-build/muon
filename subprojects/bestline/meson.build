project('bestline', 'c')

meson.override_dependency(
    'bestline',
    declare_dependency(
        include_directories: '.',
        link_with: static_library('bestline', 'bestline.c'),
    ),
)
