#include "posix.h"

#include "platform/uname.h"

#include <stdint.h>
#include <sys/utsname.h>

static struct {
	struct utsname uname;
	bool init;
} uname_info;

static bool
uname_init(void)
{
	if (uname_info.init) {
		return true;
	}

	if (uname(&uname_info.uname) == -1) {
		return false;
	}

	uname_info.init = true;
	return true;
}

bool
uname_sysname(const char **res)
{
	if (!uname_init()) {
		return false;
	}

	*res = uname_info.uname.sysname;
	return true;
}

bool
uname_machine(const char **res)
{
	if (!uname_init()) {
		return false;
	}

	*res = uname_info.uname.machine;
	return true;
}

bool
uname_endian(enum endianness *res)
{
	const uint32_t x = 1;
	if (((char *)&x)[0] == 1) {
		*res = little_endian;
	} else {
		*res = big_endian;
	}

	return true;
}