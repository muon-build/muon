# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('asm-link', 'c')

if host_machine.system() == 'windows'
    error('MESON_SKIP_TEST: link-language check relies on a POSIX shell')
endif

# A target mixing assembly with C must link as C regardless of source order.
# Before the order-independent link-selection fix, ['boot.S', 'main.c'] linked
# as assembly while ['main.c', 'boot.S'] linked as C.
executable('asmc', ['boot.S', 'main.c'])
executable('casm', ['main.c', 'boot.S'])

test('link-language-selection', find_program('check_link.sh'))
