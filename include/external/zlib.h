#ifndef MUON_EXTERNAL_ZLIB_H
#define MUON_EXTERNAL_ZLIB_H

#include <stdbool.h>
#include <stdint.h>

extern const bool have_zlib;

bool muon_zlib_extract(uint8_t *data, uint64_t len, uint8_t **unzipped, uint64_t *unzipped_len);
#endif
