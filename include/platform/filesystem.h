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

struct workspace;
struct sbuf;

bool fs_exists(const char *path);
bool fs_file_exists(const char *path);
bool fs_symlink_exists(const char *path);
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
bool fs_find_cmd(struct workspace *wk, struct sbuf *buf, const char *cmd);
bool fs_has_cmd(const char *cmd);
void fs_source_destroy(struct source *src);
void fs_source_dup(const struct source *src, struct source *dup);
bool fs_redirect(const char *path, const char *mode, int fd, int *old_fd);
bool fs_redirect_restore(int fd, int old_fd);
bool fs_copy_file(const char *src, const char *dest);
bool fs_copy_dir(const char *src_base, const char *dest_base);
bool fs_fileno(FILE *f, int *ret);
bool fs_make_symlink(const char *target, const char *path, bool force);
bool fs_fseek(FILE *file, size_t off);
bool fs_ftell(FILE *file, uint64_t *res);
const char *fs_user_home(void);
bool fs_is_a_tty(FILE *f);
bool fs_chmod(const char *path, uint32_t mode);
bool fs_copy_metadata(const char *src, const char *dest);

typedef enum iteration_result ((*fs_dir_foreach_cb)(void *_ctx, const char *path));
bool fs_dir_foreach(const char *path, void *_ctx, fs_dir_foreach_cb cb);

#ifndef S_ISVTX
#define S_ISVTX 0
#endif

#endif
