#ifndef MUON_FORMATS_INI_H
#define MUON_FORMATS_INI_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "iterator.h"
#include "platform/filesystem.h"

typedef bool ((*inihcb)(void *ctx, struct source *src, const char *sect, const char *k, const char *v, uint32_t line));

bool ini_parse(const char *path, char **buf, inihcb cb, void *octx);
#endif
