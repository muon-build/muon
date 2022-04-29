# A lot of stuff was added to this test to make it actually test that c_args
# and c_link_args are used.
project(
    'options',
    'c',
    default_options: [
        'c_args=-DMAGIC',
        'c_link_args=-lz',
    ],
)

# Test passing c_args and c_link_args options from the command line.
assert(
    get_option('c_args') == ['-DMAGIC'],
    'Incorrect value for c_args option.',
)
assert(
    get_option('c_link_args') == ['-lz'],
    'Incorrect value for c_link_args option.',
)

cc = meson.get_compiler('c')
if (
    not (
        cc.find_library(
            'z',
            required: false,
        ).found()
        or cc.find_library(
            'zlib',
            required: false,
        ).found()
        or cc.find_library(
            'zlib1',
            required: false,
        ).found()
    )
)
    error(
        'MESON_SKIP_TEST Cannot seem to find zlib via find_library, this test will probably fail.',
    )
endif

executable('main', 'main.c')
