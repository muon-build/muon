#ifndef MUON_PLATFORM_FILESYSTEM_H
#define MUON_PLATFORM_FILESYSTEM_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "iterator.h"

struct source {
	const char *label;
	const char *src;
	uint64_t len;
};

bool fs_exists(const char *path);
bool fs_file_exists(const char *path);
bool fs_exe_exists(const char *path);
bool fs_dir_exists(const char *path);
bool fs_mkdir(const char *path);
bool fs_mkdir_p(const char *path);
bool fs_read_entire_file(const char *path, struct source *src);
bool fs_fsize(FILE *file, uint64_t *ret);
bool fs_fclose(FILE *file);
FILE *fs_fopen(const char *path, const char *mode);
bool fs_fwrite(const void *ptr, size_t size, FILE *f);
bool fs_fread(void *ptr, size_t size, FILE *f);
bool fs_write(const char *path, const uint8_t *buf, uint64_t buf_len);
bool fs_find_cmd(const char *cmd, const char **ret);
void fs_source_destroy(struct source *src);
bool fs_redirect(const char *path, const char *mode, int fd, int *old_fd);
bool fs_redirect_restore(int fd, int old_fd);
bool fs_copy_file(const char *src, const char *dest);
bool fs_copy_dir(const char *src_base, const char *dest_base);

typedef enum iteration_result ((*fs_dir_foreach_cb)(void *_ctx, const char *path));
bool fs_dir_foreach(const char *path, void *_ctx, fs_dir_foreach_cb cb);
#endif
