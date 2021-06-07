#ifndef MUON_FETCH_H
#define MUON_FETCH_H

#include <stdbool.h>
#include <stdint.h>

void fetch_init(void);
void fetch_deinit(void);
bool fetch_fetch(const char *url, uint8_t **buf, uint64_t *len);
#endif
