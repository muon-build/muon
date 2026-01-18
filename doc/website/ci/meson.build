# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

foreach f : [
    'index.html',
    'style.css',
]
    configure_file(
        configuration: version_info,
        input: f,
        output: f,
    )
endforeach
