src = (
    files(
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
        'lang/eval.c',
        'lang/fmt.c',
        'lang/interpreter.c',
        'lang/lexer.c',
        'lang/object.c',
        'lang/parser.c',
        'lang/serial.c',
        'lang/string.c',
        'lang/workspace.c',
        'platform/filesystem.c',
        'platform/mem.c',
        'platform/path.c',
        'platform/run_cmd.c',
        'platform/term.c',
        'platform/uname.c',
        'args.c',
        'coerce.c',
        'compilers.c',
        'embedded.c',
        'error.c',
        'guess.c',
        'install.c',
        'log.c',
        'machine_file.c',
        'main.c',
        'opts.c',
        'rpmvercmp.c',
        'sha_256.c',
        'tests.c',
        'wrap.c',
    ) + [
        configure_file(
            configuration: version_info,
            input: 'version.c.in',
            output: 'version.c',
        ),
    ]
)

scripts = files('script/builtin_options.meson', 'script/vcs_tagger.meson')

src += [
    custom_target(
        'embedded files',
        input: scripts,
        output: 'embedded_files.h',
        capture: true,
        command: [embedder, scripts],
    ),
]

foreach d : [
    ['libcurl', {}],
    ['libarchive', {}],
    [
        'libpkgconf',
        {
            'fallback': ['pkgconf', 'dep_libpkgconf'],
            'default_options': ['pkgconf:tests=false'],
        },
    ],
    ['samurai', {'fallback': ['samurai', 'libsamu_dep']}],
]
    dep = dependency(
        d[0],
        required: get_option(d[0]),
        static: get_option('static'),
        kwargs: d[1],
    )

    summary(d[0], dep.found())

    if dep.found()
        deps += [dep]
        src += [files('external' / (d[0] + '.c'))]
    else
        src += [files('external' / (d[0] + '_null.c'))]
    endif
endforeach

platform = host_machine.system()
if platform in ['freebsd', 'linux', 'netbsd', 'openbsd', 'sunos']
    platform = 'posix'
else
    warning(
        'configuring muon for an unsupported platform "@0@"'.format(platform),
    )
    platform = 'null'
endif

foreach f : ['rpath_fixer.c']
    src += files('platform' / platform / f)
endforeach
# tracy

if get_option('tracy')
    add_languages('cpp')
    c_args += ['-DTRACY_ENABLE']
    include += include_directories([get_option('tracy_dir')])
    src += files(get_option('tracy_dir') + '/TracyClient.cpp')
endif

muon_src = src

src_platform_path = files('platform/path.c')
src_log = files('log.c')
