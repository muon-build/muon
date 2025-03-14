# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

external_deps = []
dep_sources = [
    files('tinyjson.c'),
]

dep_dict = {}

foreach d : [
    ['libcurl'],
    ['libarchive'],
    [
        'libpkgconf',
        {
            'fallback': ['pkgconf', 'dep_libpkgconf'],
            'default_options': ['pkgconf:tests=false', 'warning_level=1'],
        },
    ],
]
    name = d[0]
    kwargs = d.get(1, {})
    if get_option('static')
        kwargs += {'static': true}
    endif

    dep = dependency(name, required: get_option(name), kwargs: kwargs)

    summary(name, dep.found())

    dep_dict += {name: dep.found()}

    if dep.found()
        external_deps += [dep]
        dep_sources += [files((name + '.c'))]
    else
        dep_sources += [files((name + '_null.c'))]
    endif
endforeach

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
    dep = dependency(readline_opt)
    summary(readline_opt, dep.found())
    dep_dict += {name: dep.found()}
    external_deps += [dep]
endif
