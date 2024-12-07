add_languages('cpp')

deps += [
    dependency('glad'),
    dependency('glfw3'),
    dependency('imgui', default_options: {
        'imgui_user_config': meson.project_source_root() / 'include/ui/imconfig.h',
    }),
]

src += files(
    'ui.cpp',
)
