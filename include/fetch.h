#ifndef BOSON_FETCH_H
#define BOSON_FETCH_H
#include <stdbool.h>

void fetch_init(void);
void fetch_deinit(void);
bool fetch_fetch(const char *url, const char *out_path);
#endif
