#ifndef MUON_TAR_H
#define MUON_TAR_H

#include <stdint.h>
#include <stdbool.h>

bool untar(uint8_t *data, uint64_t len, const char *destdir);
#endif
