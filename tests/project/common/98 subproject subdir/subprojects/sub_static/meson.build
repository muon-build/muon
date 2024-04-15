project('sub_static', default_options: ['default_library=shared'])

assert(get_option('default_library') == 'static')
meson.override_dependency('sub_static', declare_dependency())
meson.override_dependency('sub_static2', declare_dependency(), static: true)
meson.override_dependency(
    'sub_static3',
    declare_dependency(variables: {'static': 'true'}),
    static: true,
)
meson.override_dependency(
    'sub_static3',
    declare_dependency(variables: {'static': 'false'}),
    static: false,
)
