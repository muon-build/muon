#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>

bool
term_winsize(int fd, uint32_t *height, uint32_t *width)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	HANDLE h;
	DWORD mode;

	*height = 24;
	*width = 80;

	/* if not a console, or a terminal using conpty, use default values */
	h = (HANDLE *)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		return true;
	}
	if (!GetConsoleMode(h, &mode)) {
		return true;
	}

	/* otherwise, retrieve the geometry */
	if (GetConsoleScreenBufferInfo(h, &csbi)) {
		*height = csbi.dwSize.Y;
		*width = csbi.dwSize.X;
		return true;
	}

	return false;
}
