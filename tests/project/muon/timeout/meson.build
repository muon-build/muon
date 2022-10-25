# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('timeout', 'c')

test('basic timeout', find_program('sleep'), args: '100', timeout: 1)
test('hard-to-kill', executable('hang', 'hang.c'), timeout: 1)
