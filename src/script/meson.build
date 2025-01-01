# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

scripts_input = []
scripts_cmdline = []
embedded_scripts = []

foreach s : [
    'commands/copyfile.meson',
    'commands/coverage.meson',
    'commands/delete_suffix.meson',
    'commands/i18n/gettext.meson',
    'commands/i18n/itstool.meson',
    'commands/i18n/msgfmthelper.meson',
    'commands/vcs_tagger.meson',
    'html/test_out.html',
    'lib/cmake_prelude.meson',
    'modules/_test.meson',
    'modules/gnome.meson',
    'modules/i18n.meson',
    'options/global.meson',
    'options/per_project.meson',
    'python/python_info.py',
    'runtime/dependencies.meson',
]
    scripts_input += s
    scripts_cmdline += files(s) + [s]
endforeach

script_sources = custom_target(
    'embedded files',
    input: scripts_input,
    output: 'embedded_files.h',
    capture: true,
    command: [embedder, scripts_cmdline],
)


embedded_scripts = scripts_input
embedded_scripts_base = meson.current_source_dir()
