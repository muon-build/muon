#ifndef MUON_PLATFORM_PATH_H
#define MUON_PLATFORM_PATH_H
#include <stdbool.h>
#include <stdint.h>

#define PATH_SEP '/'

struct workspace;
struct sbuf;

void path_init(void);
void path_deinit(void);
void path_copy(struct workspace *wk, struct sbuf *sb, const char *path);
void path_cwd(struct workspace *wk, struct sbuf *sb);
bool path_chdir(const char *path);
bool path_is_absolute(const char *path);
void path_push(struct workspace *wk, struct sbuf *sb, const char *b);
void path_join(struct workspace *wk, struct sbuf *sb, const char *a, const char *b);
// like path_join but won't discard a if b is an absolute path
void path_join_absolute(struct workspace *wk, struct sbuf *sb, const char *a, const char *b);
bool path_make_absolute(char *buf, uint32_t len, const char *path);
bool path_relative_to(char *buf, uint32_t len, const char *base_raw, const char *path_raw);
bool path_is_basename(const char *path);
bool path_without_ext(char *buf, uint32_t len, const char *path);
bool path_basename(char *buf, uint32_t len, const char *path);
bool path_dirname(char *buf, uint32_t len, const char *path);
bool path_is_subpath(const char *base, const char *sub);
bool path_add_suffix(char *path, uint32_t len, const char *suff);
bool path_executable(char *buf, uint32_t len, const char *path);
void path_normalize(char *buf, bool optimize);
void _path_normalize(struct workspace *wk, struct sbuf *buf, bool optimize);
#endif
