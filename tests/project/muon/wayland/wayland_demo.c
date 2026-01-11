/**
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <wayland-client.h>

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (display) {
        puts("wayland_demo: wl_display_connect() succeeded");
        wl_display_disconnect(display);
    } else {
        puts("wayland_demo: wl_display_connect() returned NULL (no compositor?)");
    }
    return 0;
}
