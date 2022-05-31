scripts = files(
    'global_options.meson',
    'per_project_options.meson',
    'vcs_tagger.meson',
)

script_sources = custom_target(
    'embedded files',
    input: scripts,
    output: 'embedded_files.h',
    capture: true,
    command: [embedder, scripts],
)

