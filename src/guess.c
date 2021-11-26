#include "posix.h"

#include <assert.h>

#include "guess.h"

bool
guess_version(struct workspace *wk, const char *src, obj *res)
{
	const char *ver, *ver_end = NULL;
	for (ver = src; *ver; ++ver) {
		ver_end = ver;
		while (('0' <= *ver_end && *ver_end <= '9') || *ver_end == '.') {
			++ver_end;
		}

		if (ver != ver_end) {
			break;
		}
	}

	if (!ver_end) {
		return false;
	}

	assert(ver_end >= ver);

	uint32_t len = ver_end - ver;

	if (len) {
		obj s = make_strn(wk, ver, len);
		*res = s;
		return true;
	} else {
		return false;
	}
}
