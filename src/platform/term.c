#include "posix.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "platform/term.h"
#include "log.h"

bool
term_winsize(int fd, uint32_t *height, uint32_t *width)
{
	if (!term_isterm(fd)) {
		*height = 24;
		*width = 80;
		return true;
	}

	struct winsize w;
	if (ioctl(fd, TIOCGWINSZ, &w) == -1) {
		LOG_E("failed to get winsize: %s", strerror(errno));
		return false;
	}

	*height = w.ws_row;
	*width = w.ws_col;
	return true;
}

bool
term_isterm(int fd)
{
	return isatty(fd) == 1;
}
