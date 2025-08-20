# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

add_languages('cpp')

add_project_arguments(
    meson.get_compiler('cpp').get_supported_arguments(project_arguments + ['-Wno-c99-extensions']),
    language: ['cpp'],
)

deps += [
    dependency('glad'),
    dependency('glfw3'),
    dependency(
        'imgui',
        default_options: {
            'imgui_user_config': meson.project_source_root() / 'include/ui/imconfig.h',
        },
    ),
]

src += files(
    'ui.cpp',
)
