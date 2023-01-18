# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

external_deps = []
dep_sources = []

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
    ['samurai', {'fallback': ['samurai', 'libsamu_dep']}],
    ['bestline'],
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

if get_option('static') and dep_dict['libcurl']
    external_deps += dependency('libbrotlidec', static: true)
endif
