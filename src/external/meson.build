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

if get_option('pkgconfig') == 'exec'
    dep_sources += [files('pkgconfig_exec.c')]
    summary('pkgconfig', 'exec')
elif get_option('pkgconfig') == 'none'
    dep_sources += [files('pkgconfig_null.c')]
    summary('pkgconfig', 'none')
else
    is_auto = get_option('pkgconfig') == 'auto'
    dep_libpkgconf = dependency(
        'libpkgconf',
        required: is_auto != true,
        fallback: ['pkgconf', 'dep_libpkgconf'],
        default_options: {
            'tests': false,
            'warning_level': '1',
        },
        kwargs: dependency_kwargs,
    )

    if dep_libpkgconf.found()
        dep_dict += {'libpkgconf': true}
        external_deps += [dep_libpkgconf]
        dep_sources += [files('libpkgconf.c')]
        summary('pkgconfig', is_auto ? 'auto (libpkgconf)' : 'libpkgconf')
    else
        dep_sources += [files('pkgconfig_null.c')]
        summary('pkgconfig', 'none')
    endif
endif

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
