#ifndef INIH_H
#define INIH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "iterator.h"

typedef bool ((*inihcb)(void *ctx, const char *path, const char *sect, const char *k, const char *v, uint32_t line));

bool ini_parse(const char *path, char **buf, inihcb cb, void *octx);
#endif
