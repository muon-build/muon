# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

embedder_c_args = []

if cc.compiles(
'''#if !(defined(__has_embed) && __STDC_VERSION__ >= 202000L)
#error ""
#endif
''')
    embedder_c_args += '-DMUON_HAVE_EMBED'
endif

embedder = executable(
    'embedder',
    'embedder.c',
    include_directories: include_dir,
    native: true,
    c_args: embedder_c_args,
)
