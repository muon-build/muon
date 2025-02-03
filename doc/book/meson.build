mdbook = find_program('mdbook')

custom_target(
    output: 'reference.html',
    command: [muon, 'internal', 'dump_docs'],
    capture: true,
    build_by_default: true,
)

custom_target(
    output: 'book',
    input: files(
        'src/SUMMARY.md',
        'src/chapter_1.md',
        'src/contributing.md',
        'src/differences.md',
        'src/features.md',
        'src/internals.md',
        'src/intro.md',
        'src/release_notes.md',
        'src/scripting.md',
        'src/scripting/adding_new_builtin.md',
        'src/scripting/additional_functions.md',
        'src/scripting/function_behavior_changes.md',
        'src/scripting/function_declaration.md',
        'src/scripting/module_structure.md',
        'src/scripting/mutability.md',
        'src/scripting/rationale.md',
        'src/scripting/scope.md',
        'src/scripting/type_specifiers.md',
        'theme/book.js',
        'theme/css/variables.css',
        'book.toml',
    ),
    command: [
        mdbook,
        'build',
        meson.current_source_dir(),
        '-d', meson.current_build_dir(),
    ],
    build_by_default: true,
)

foreach ext : ['svg', 'png']
    fs.copyfile('../website/muon_logo.svg', 'favicon.@0@'.format(ext))
endforeach
