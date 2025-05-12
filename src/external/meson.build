# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

external_deps = []
dep_sources = [
    files('tinyjson.c'),
]

dependency_kwargs = {}
if get_option('static')
    dependency_kwargs = {'static': true}
endif

dep_dict = {}

foreach d : [
    ['libcurl'],
    ['libarchive'],
]
    name = d[0]

    dep = dependency(name, required: get_option(name), kwargs: dependency_kwargs)

    summary(name, dep.found())

    dep_dict += {name: dep.found()}

    if dep.found()
        external_deps += [dep]
        dep_sources += [files((name + '.c'))]
    else
        dep_sources += [files((name + '_null.c'))]
    endif
endforeach

dep_sources += files(
    'pkgconfig.c',
    'pkgconfig_exec.c',
)
pkgconfig_impls = ['null', 'exec']

if not get_option('libpkgconf').disabled()
    dep_libpkgconf = dependency(
        'libpkgconf',
        required: get_option('libpkgconf'),
        default_options: {
            'tests': 'disabled',
            'warning_level': '1',
        },
        kwargs: dependency_kwargs,
        version: '>=1.9.0',
    )

    if dep_libpkgconf.found()
        dep_dict += {'libpkgconf': true}
        external_deps += declare_dependency(
            compile_args: ['-DMUON_HAVE_LIBPKGCONF=1'],
            dependencies: dep_libpkgconf,
        )
        dep_sources += files('pkgconfig_libpkgconf.c')
        pkgconfig_impls += 'libpkgconf'
    endif
endif

summary('pkgconfig', pkgconfig_impls)

if get_option('samurai').disabled()
    dep_dict += {'samurai': false}
    dep_sources += files('samurai_null.c')
else
    dep_dict += {'samurai': true}
    dep_sources += files(
        'samurai/build.c',
        'samurai/deps.c',
        'samurai/env.c',
        'samurai/graph.c',
        'samurai/htab.c',
        'samurai/log.c',
        'samurai/parse.c',
        'samurai/samu.c',
        'samurai/scan.c',
        'samurai/tool.c',
        'samurai/tree.c',
        'samurai/util.c',
        'samurai.c',
    )
endif

readline_opt = get_option('readline')
dep_sources += files(f'readline_@readline_opt@.c')
if readline_opt != 'builtin'
    dep = dependency(
        readline_opt,
        kwargs: dependency_kwargs,
    )
    summary(readline_opt, dep.found())
    dep_dict += {name: dep.found()}
    external_deps += [dep]
endif
