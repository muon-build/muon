scripts_input = []
scripts_cmdline = []

foreach s : [
    'global_options.meson',
    'per_project_options.meson',
    'vcs_tagger.meson',
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

