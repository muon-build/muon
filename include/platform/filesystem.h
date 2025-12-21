/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_FILESYSTEM_H
#define MUON_PLATFORM_FILESYSTEM_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "iterator.h"
#include "lang/source.h"
#include "lang/types.h"

enum fs_mkdir_result {
	fs_mkdir_result_error,
	fs_mkdir_result_ok,
	fs_mkdir_result_exists,
};

struct workspace;
struct arena;
struct tstr;

bool fs_stat(const char *path, struct stat *sb);
enum fs_mtime_result { fs_mtime_result_ok, fs_mtime_result_not_found, fs_mtime_result_err };
enum fs_mtime_result fs_mtime(const char *path, int64_t *mtime);
bool fs_exists(const char *path);
bool fs_file_exists(const char *path);
bool fs_symlink_exists(const char *path);
bool fs_exe_exists(struct workspace *wk, const char *path);
bool fs_dir_exists(const char *path);
enum fs_mkdir_result fs_mkdir(const char *path, bool exist_ok);
bool fs_mkdir_p(struct workspace *wk, const char *path);
bool fs_mkdir_p_recorded(struct workspace *wk, const char *path, obj record);
bool fs_rmdir(const char *path, bool force);
bool fs_rmdir_recursive(struct workspace *wk, const char *path, bool force);
bool fs_read_entire_file(struct arena *a, const char *path, struct source *src);
bool fs_fsize(FILE *file, uint64_t *ret);
bool fs_fclose(FILE *file);
FILE *fs_fopen(const char *path, const char *mode);
bool fs_fwrite(const void *ptr, size_t size, FILE *f);
bool fs_fread(void *ptr, size_t size, FILE *f);
int32_t fs_read(int fd, void *buf, uint32_t buf_len);
bool fs_write(const char *path, const uint8_t *buf, uint64_t buf_len);
bool fs_find_cmd(struct workspace *wk, struct tstr *buf, const char *cmd);
void fs_source_dup(struct arena *a, const struct source *src, struct source *dup);
bool fs_copy_file(struct workspace *wk, const char *src, const char *dest, bool force);
struct fs_copy_dir_ctx {
	struct workspace *wk;
	void (*file_cb)(void *usr_ctx, const char *src, const char *dest);
	void *usr_ctx;
	const char *src_base, *dest_base;
	bool force;
};
bool fs_copy_dir_ctx(struct workspace *wk, struct fs_copy_dir_ctx *ctx);
bool fs_copy_dir(struct workspace *wk, const char *src_base, const char *dest_base, bool force);
enum iteration_result fs_copy_dir_iter(void *_ctx, const char *path);
bool fs_fileno(FILE *f, int *ret);
bool fs_make_symlink(const char *target, const char *path, bool force);
bool fs_fseek(FILE *file, size_t off);
bool fs_ftell(FILE *file, uint64_t *res);
const char *fs_user_home(void);
bool fs_is_a_tty_from_fd(struct workspace *wk, int fd);
bool fs_is_a_tty(struct workspace *wk, FILE *f);
bool fs_chmod(const char *path, uint32_t mode);
bool fs_copy_metadata(const char *src, const char *dest);
bool fs_remove(const char *path);
bool fs_has_extension(const char *path, const char *ext);
FILE *fs_make_tmp_file(const char *name, const char *suffix, char *buf, uint32_t len);
bool fs_make_writeable_if_exists(const char *path);
bool fs_wait_for_input(int fd, uint32_t *bytes_available);

typedef enum iteration_result((*fs_dir_foreach_cb)(void *_ctx, const char *path));
bool fs_dir_foreach(struct workspace *wk, const char *path, void *_ctx, fs_dir_foreach_cb cb);

bool fs_path_state_base(struct workspace *wk, struct tstr *path, bool mkdir);
bool fs_path_config_base(struct workspace *wk, struct tstr *path, bool mkdir);

#ifndef S_ISGID
#define S_ISGID 0
#endif

#ifndef S_ISUID
#define S_ISUID 0
#endif

#ifndef S_ISVTX
#define S_ISVTX 0
#endif

#endif
