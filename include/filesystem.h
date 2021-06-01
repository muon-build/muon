#ifndef BOSON_FILESYSTEM_H
#define BOSON_FILESYSTEM_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

bool fs_file_exists(const char *path);
bool fs_dir_exists(const char *path);
bool fs_mkdir(const char *path);
bool fs_mkdir_p(const char *path);
bool fs_read_entire_file(const char *path, char **buf, uint64_t *size);
bool fs_fsize(FILE *file, uint64_t *ret);
bool fs_fclose(FILE *file);
FILE *fs_fopen(const char *path, const char *mode);
bool fs_write(const char *path, const uint8_t *buf, uint64_t buf_len);
bool fs_find_cmd(const char *cmd, char **ret);
#endif
