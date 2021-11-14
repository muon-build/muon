#ifndef MUON_PLATFORM_UNAME_H
#define MUON_PLATFORM_UNAME_H

#include <stdbool.h>

enum endianness {
	big_endian,
	little_endian,
};

bool uname_sysname(const char **res);
bool uname_machine(const char **res);
bool uname_endian(enum endianness *res);
#endif
