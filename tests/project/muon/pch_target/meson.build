project('pch_target')

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
