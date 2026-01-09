# SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

project(
    'muon-pcap-tests',
    'c',
    version: '0.1.0',
    meson_version: '>=0.60.0',
)

pcap_dep = dependency('pcap', required: true)

test_exe = executable(
    'pcap_test',
    'pcap_test.c',
    dependencies: [pcap_dep],
)

test(
    'pcap_test',
    test_exe,
)
