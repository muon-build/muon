project(
    'objc and objcpp',
    'objc',
    'objcpp',
    default_options: [
        'buildtype=debugoptimized',
    ],
)

if host_machine.system() != 'darwin'
    error('MESON_SKIP_TEST: test must be run on darwin')
endif

OBJC_ARGS = [
    '-mmacosx-version-min=10.12',
    '-fobjc-arc',
]

OBJCPP_ARGS = OBJC_ARGS + [
    '-std=c++17',
]

INCLUDE_DIRECTORIES = [
    include_directories('./interfaces'),
]

LINK_ARGS = [
    '-Wl,-framework,Foundation',
]

test(
    'test-objc',
    executable(
        'objc-demo',
        [
            'objc-main.m',
            'calc.m',
        ],
        include_directories: INCLUDE_DIRECTORIES,
        objc_args: OBJC_ARGS,
        link_args: LINK_ARGS,
    ),
)

test(
    'test-objcpp',
    executable(
        'objcpp-demo',
        [
            'objcpp-main.mm',
            'calc.m',
        ],
        include_directories: INCLUDE_DIRECTORIES,
        objc_args: OBJC_ARGS,
        objcpp_args: OBJCPP_ARGS,
        link_args: OBJCPP_ARGS + LINK_ARGS,
    ),
)
