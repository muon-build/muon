# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

# Increased timeout necessary to avoid sporadic timeouts on GitHub ubuntu workflow
test('fmt', find_program('test.sh'), args: muon, suite: 'fmt', timeout: 45)

subdir('editorconfig')
