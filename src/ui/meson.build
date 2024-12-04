add_languages('cpp')

subproject('glfw3')
subproject('glad')

deps += [
    dependency('glad'),
    dependency('glfw3'),
    dependency('imgui', default_options: {
        'imgui_user_config': meson.project_source_root() / 'include/ui/imconfig.h',
    }),
    declare_dependency(
        compile_args: '-DIMGUI_FONT_PATH="@0@"'.format(meson.project_source_root() / 'subprojects/DearImGui/misc/fonts')
    )
]

src += files(
    'ui.cpp',
    'inspector.cpp',
)
