# TODO vulkan

sources = [
    'context.c',
    'init.c',
    'input.c',
    'monitor.c',
    'vulkan.c',
    'window.c',
    'platform.c',

    'null_init.c',
    'null_joystick.c',
    'null_monitor.c',
    'null_window.c',
]

dependencies = [threads_dep]

if host_system == 'linux' or host_system == 'bsd'
    dependencies += [math_dep]

    if backend == 'x11'
        dependencies += [cc.find_library('dl', required: false)]
    endif
endif

conf_data = configuration_data()
foreach b : ['cocoa', 'win32', 'x11', 'wayland', 'osmesa']
    conf_data.set('_GLFW_' + b.to_upper(), backend == b)
endforeach
conf_data.set('_GLFW_BUILD_DLL', get_option('default_library') == 'shared')
conf_data.set('_GLFW_VULKAN_STATIC', get_option('glfw_vulkan_static'))
conf_data.set('_GLFW_USE_HYBRID_HPG', get_option('glfw_use_hybrid_hpg'))
conf_file = configure_file(configuration: conf_data, output: 'glfw_config.h')
sources += conf_file

if (backend == 'wayland' or backend == 'x11')
    if host_system == 'linux'
        sources += ['linux_joystick.c']
    else
        sources += ['null_joystick.c']
    endif
endif

if backend == 'cocoa'
    add_languages('objc', native: false)

    dependencies += dependency(
        'appleframeworks',
        modules: ['Cocoa', 'IOKit', 'CoreFoundation', 'CoreVideo'],
    )

    sources += [
        'cocoa_init.m',
        'cocoa_joystick.m',
        'cocoa_monitor.m',
        'cocoa_time.c',
        'cocoa_window.m',
        'egl_context.c',
        'nsgl_context.m',
        'osmesa_context.c',
        'posix_thread.c',
        'posix_module.c',
    ]
elif backend == 'win32'
    dependencies += cc.find_library('gdi32')

    sources += [
        'egl_context.c',
        'osmesa_context.c',
        'wgl_context.c',
        'win32_init.c',
        'win32_joystick.c',
        'win32_monitor.c',
        'win32_thread.c',
        'win32_time.c',
        'win32_window.c',
    ]
elif backend == 'x11'
    dependencies += dependency('x11')
    # TODO: XRandR header found?
    # TODO: Xinerama header found?
    # TODO: Xkb header found?
    # TODO: Xcursor header found?
    sources += [
        'egl_context.c',
        'glx_context.c',
        'osmesa_context.c',
        'posix_module.c',
        'posix_poll.c',
        'posix_thread.c',
        'posix_time.c',
        'x11_init.c',
        'x11_monitor.c',
        'x11_window.c',
        'xkb_unicode.c',
    ]
elif backend == 'wayland'
    wayland_client = dependency('wayland-client')

    wayland_scanner_dep = dependency('wayland-scanner', required: false, native: true)
    if wayland_scanner_dep.found()
        wayland_scanner = find_program(
            wayland_scanner_dep.get_pkgconfig_variable('wayland_scanner'),
            native: true,
        )
    else
        wayland_scanner = find_program('wayland-scanner', native: true)
    endif

    protocols = [
        'wayland',
        'xdg-shell',
        'xdg-decoration-unstable-v1',
        'viewporter',
        'relative-pointer-unstable-v1',
        'pointer-constraints-unstable-v1',
        'idle-inhibit-unstable-v1',
        'xdg-activation-v1',
        'fractional-scale-v1',
    ]

    wl_protos_src = []
    wl_protos_headers = []

    foreach name : protocols
        xml = meson.project_source_root() / 'deps/wayland' / f'@name@.xml'
        outsrc = f'@name@-client-protocol-code.h'
        outhdr = f'@name@-client-protocol.h'

        wl_protos_src += custom_target(
            xml.underscorify() + '_server_c',
            input: xml,
            output: outsrc,
            command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
        )

        wl_protos_headers += custom_target(
            xml.underscorify() + '_server_h',
            input: xml,
            output: outhdr,
            command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
        )
    endforeach

    dependencies += [
        wayland_client,
        dependency('wayland-egl'),
        dependency('xkbcommon')
    ]

    sources += [
        'egl_context.c',
        'osmesa_context.c',
        'posix_thread.c',
        'posix_time.c',
        'posix_poll.c',
        'posix_module.c',
        'wl_init.c',
        'wl_monitor.c',
        'wl_window.c',
        'xkb_unicode.c',
        'xkb_unicode.h',
    ] + wl_protos_src + wl_protos_headers
elif backend == 'osmesa'
    dependencies += dependency('osmesa')
    sources += [
        'null_init.c',
        'null_joystick.c',
        'null_monitor.c',
        'null_window.c',
        'osmesa_context.c',
        'posix_thread.c',
        'posix_time.c',
    ]
endif

include = [include_directories('../include'), include_directories('.')]

glfw3_library = library(
    'glfw',
    sources,
    version: meson.project_version(),
    c_args: ['-D_GLFW_USE_CONFIG_H'],
    objc_args: ['-D_GLFW_USE_CONFIG_H'],
    dependencies: dependencies,
    include_directories: include,
)

glfw3_dep = declare_dependency(
    link_with: [glfw3_library],
    include_directories: include,
    dependencies: dependencies,
)

meson.override_dependency('glfw3', glfw3_dep)
