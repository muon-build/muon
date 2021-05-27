#ifndef SHA_256_H
#define SHA_256_H

#include <stdint.h>
#include <string.h>

void calc_sha_256(uint8_t hash[32], const void *input, size_t len);

#endif
