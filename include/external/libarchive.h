#ifndef MUON_EXTERNAL_LIBARCHIVE_H
#define MUON_EXTERNAL_LIBARCHIVE_H

#include <stddef.h>
#include <stdbool.h>

extern const bool have_libarchive;

bool muon_archive_extract(const char *buf, size_t size, const char *dest_path);
#endif
