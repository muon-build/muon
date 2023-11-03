project('tiny-json', 'c')

meson.override_dependency(
    'tinyjson',
    declare_dependency(
        include_directories: '.',
        link_with: static_library('tiny-json', 'tiny-json.c'),
    ),
)
