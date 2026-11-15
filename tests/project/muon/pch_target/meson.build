project(
    'pch_target',
    default_options: {
        'b_staticpic': false,
        'b_pie': false,
    },
)

add_languages('c', native: false)

pch = static_library(
    'pch_target',
    c_pch: 'pch.h',
    build_by_default: false,
)

executable(
    'foo',
    c_pch: pch,
    sources: ['foo.c'],
)

executable(
    'bar',
    c_pch: pch,
    sources: ['bar.c'],
)
