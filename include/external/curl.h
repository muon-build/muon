#ifndef MUON_EXTERNAL_CURL_H
#define MUON_EXTERNAL_CURL_H

#include <stdbool.h>
#include <stdint.h>

extern const bool have_curl;

void muon_curl_init(void);
void muon_curl_deinit(void);
bool muon_curl_fetch(const char *url, uint8_t **buf, uint64_t *len);
#endif
