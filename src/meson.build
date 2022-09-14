src = files(
    'backend/backend.c',
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
    'functions/dependency.c',
    'functions/dict.c',
    'functions/disabler.c',
    'functions/environment.c',
    'functions/external_program.c',
    'functions/feature_opt.c',
    'functions/file.c',
    'functions/generator.c',
    'functions/kernel.c',
    'functions/kernel/build_target.c',
    'functions/kernel/configure_file.c',
    'functions/kernel/custom_target.c',
    'functions/kernel/dependency.c',
    'functions/kernel/install.c',
    'functions/kernel/options.c',
    'functions/kernel/subproject.c',
    'functions/machine.c',
    'functions/meson.c',
    'functions/modules.c',
    'functions/modules/fs.c',
    'functions/modules/keyval.c',
    'functions/modules/pkgconfig.c',
    'functions/modules/python.c',
    'functions/modules/sourceset.c',
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
    'cmd_install.c',
    'cmd_test.c',
    'coerce.c',
    'compilers.c',
    'embedded.c',
    'error.c',
    'guess.c',
    'install.c',
    'log.c',
    'machine_file.c',
    'main.c',
    'options.c',
    'opts.c',
    'rpmvercmp.c',
    'sha_256.c',
    'wrap.c',
)

deps = []

subdir('platform')
src += platform_sources

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
