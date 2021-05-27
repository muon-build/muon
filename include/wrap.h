#ifndef BOSON_WRAP_H
#define BOSON_WRAP_H
#include <stdbool.h>
#include <stdint.h>

struct workspace;

bool wrap_handle(struct workspace *wk, uint32_t n_id, const char *wrap_file, const char *dest_path);
#endif
