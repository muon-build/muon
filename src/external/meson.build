external_deps = []
dep_sources = []

dep_dict = {}

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

    kwargs = d[1]
    if get_option('static')
        kwargs += {'static': true}
    endif

    dep = dependency(d[0], required: get_option(d[0]), kwargs: kwargs)

    summary(d[0], dep.found())

    dep_dict += {d[0]: dep.found()}

    if dep.found()
        external_deps += [dep]
        dep_sources += [files((d[0] + '.c'))]
    else
        dep_sources += [files((d[0] + '_null.c'))]
    endif
endforeach

if get_option('static') and dep_dict['libcurl']
    external_deps += dependency('libbrotlidec', static: true)
endif
