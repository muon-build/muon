# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project('test wayland meson module', 'c')

wl = import('wayland')

wl_dep = dependency('wayland-client', required: false)
wl_scanner = dependency('wayland-scanner', required: false, native: true)
if wl_dep.found() and wl_scanner.found()
    xml = wl.find_protocol('xdg-shell', state: 'stable')
    gen = wl.scan_xml(xml)

    executable('wayland_demo', gen + ['wayland_demo.c'], dependencies: wl_dep)
endif
