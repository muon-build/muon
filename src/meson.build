src = files(
    'backend/common_args.c',
    'backend/ninja.c',
    'backend/ninja/alias_target.c',
    'backend/ninja/build_target.c',
    'backend/ninja/custom_target.c',
    'backend/ninja/rules.c',
    'backend/output.c',
    'data/bucket_array.c',
    'data/darr.c',
    'data/hash.c',
    'formats/ini.c',
    'functions/array.c',
    'functions/boolean.c',
    'functions/both_libs.c',
    'functions/build_target.c',
    'functions/common.c',
    'functions/compiler.c',
    'functions/configuration_data.c',
    'functions/custom_target.c',
    'functions/default.c',
    'functions/default/build_target.c',
    'functions/default/configure_file.c',
    'functions/default/custom_target.c',
    'functions/default/dependency.c',
    'functions/default/options.c',
    'functions/default/setup.c',
    'functions/default/subproject.c',
    'functions/dependency.c',
    'functions/dict.c',
    'functions/disabler.c',
    'functions/environment.c',
    'functions/external_library.c',
    'functions/external_program.c',
    'functions/feature_opt.c',
    'functions/file.c',
    'functions/generator.c',
    'functions/machine.c',
    'functions/meson.c',
    'functions/modules.c',
    'functions/modules/fs.c',
    'functions/modules/pkgconfig.c',
    'functions/modules/python.c',
    'functions/number.c',
    'functions/run_result.c',
    'functions/string.c',
    'functions/subproject.c',
    'lang/analyze.c',
    'lang/eval.c',
    'lang/fmt.c',
    'lang/interpreter.c',
    'lang/lexer.c',
    'lang/object.c',
    'lang/parser.c',
    'lang/serial.c',
    'lang/string.c',
    'lang/workspace.c',
    'args.c',
    'coerce.c',
    'compilers.c',
    'embedded.c',
    'error.c',
    'guess.c',
    'install.c',
    'log.c',
    'machine_file.c',
    'options.c',
    'opts.c',
    'rpmvercmp.c',
    'sha_256.c',
    'tests.c',
    'wrap.c',
)

deps = [platform_dep]

# version information
src += configure_file(
    configuration: version_info,
    input: 'version.c.in',
    output: 'version.c',
)

# embedded scripts
subdir('script')
src += script_sources

# dependencies
subdir('external')
src += dep_sources
deps += external_deps

muon_lib = static_library(
    'muon',
    src,
    dependencies: deps,
    include_directories: [include_dir],
)

muon_dep = declare_dependency(
    link_with: muon_lib,
    dependencies: deps,
    include_directories: [include_dir],
)
