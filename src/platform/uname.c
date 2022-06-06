#include "posix.h"

#include "platform/uname.h"

#include <stdint.h>

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
