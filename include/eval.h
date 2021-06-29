#ifndef MUON_RUNTIME_H
#define MUON_RUNTIME_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

struct workspace;
struct source;
struct darr;

enum language_mode {
	language_external,
	language_internal,
	language_opts,
	language_mode_count,
};

struct source_data {
	char *data;
	uint64_t data_len;
};

union token_data {
	const char *s;
	int64_t n;
};

void source_data_destroy(struct source_data *sdata);
bool eval_project(struct workspace *wk, const char *subproject_name,
	const char *cwd, const char *build_dir, uint32_t *proj_id);
bool eval_project_file(struct workspace *wk, const char *src);
bool eval(struct workspace *wk, struct source *src, uint32_t *obj);
bool eval_str(struct workspace *wk, const char *str, uint32_t *obj);
void error_message(struct source *src, uint32_t line, uint32_t col, const char *fmt, va_list args);
void error_messagef(struct source *src, uint32_t line, uint32_t col, const char *fmt, ...)
__attribute__ ((format(printf, 4, 5)));
#endif
