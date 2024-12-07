/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_UI_UI_H
#define MUON_UI_UI_H

struct g_win {
	struct GLFWwindow *window;
};

void ui_update(void);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

#endif
