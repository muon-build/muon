#ifndef MUON_PLATFORM_PATH_H
#define MUON_PLATFORM_PATH_H
#include <stdbool.h>
#include <stdint.h>

#define PATH_SEP '/'

#ifdef _WIN32
#define ENV_PATH_SEP ';'
#else
#define ENV_PATH_SEP ':'
#endif

bool path_init(void);
bool path_cwd(char *buf, uint32_t len);
bool path_chdir(const char *path);
bool path_is_absolute(const char *path);
bool path_join(char *buf, uint32_t len, const char *a, const char *b);
// like path_join but won't discard a if b is an absolute path
bool path_join_absolute(char *buf, uint32_t len, const char *a, const char *b);
bool path_make_absolute(char *buf, uint32_t len, const char *path);
bool path_relative_to(char *buf, uint32_t len, const char *base_raw, const char *path_raw);
bool path_is_basename(const char *path);
bool path_without_ext(char *buf, uint32_t len, const char *path);
bool path_basename(char *buf, uint32_t len, const char *path);
bool path_dirname(char *buf, uint32_t len, const char *path);
bool path_is_subpath(const char *base, const char *sub);
bool path_add_suffix(char *path, uint32_t len, const char *suff);
bool path_executable(char *buf, uint32_t len, const char *path);
#endif
