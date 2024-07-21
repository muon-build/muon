/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EXTERNAL_READLINE_H
#define MUON_EXTERNAL_READLINE_H

char *muon_readline(const char *prompt);
int muon_readline_history_add(const char *line);
void muon_readline_history_free(void);
#endif
