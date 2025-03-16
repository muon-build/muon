project('bestline', 'c')

bestline_dep = declare_dependency(
    include_directories: '.',
    link_with: static_library('bestline', 'bestline.c'),
)

meson.override_dependency(
    'bestline',
    bestline_dep,
)
