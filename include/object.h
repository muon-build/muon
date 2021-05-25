#ifndef BOSON_OBJECT_H
#define BOSON_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "iterator.h"

struct workspace;

enum obj_type {
	obj_any, // used for argument type checking
	obj_default,
	obj_null,
	obj_string,
	obj_number,
	obj_compiler,
	obj_meson,
	obj_array,
	obj_bool,
	obj_file,
	obj_build_target,
	obj_subproject,
	obj_dependency,
	obj_type_count,
};

enum tgt_type {
	tgt_executable,
	tgt_library
};

struct obj {
	enum obj_type type;
	union {
		uint64_t n;
		uint32_t str;
		int64_t num;
		bool boolean;
		struct {
			uint32_t l;
			uint32_t r;
			uint32_t tail;
			uint32_t len;
			bool have_r;
		} arr;
		uint32_t file;
		struct {
			uint32_t name, build_name;
			enum tgt_type type;
			uint32_t src;
			uint32_t link_with;
			uint32_t include_directories;
			uint32_t deps;
			uint32_t c_args;
		} tgt;
		struct {
			uint32_t name;
			uint32_t link_with;
			uint32_t include_directories;
		} dep;
		uint32_t subproj;
	} dat;
};


const char *obj_type_to_s(enum obj_type t);
void obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id);

typedef enum iteration_result (*obj_array_iterator)(struct workspace *wk, void *ctx, uint32_t val);
bool obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb);

bool obj_equal(struct workspace *wk, uint32_t l, uint32_t r);
bool obj_array_in(struct workspace *wk, uint32_t l_id, uint32_t r_id, bool *res);
bool obj_array_index(struct workspace *wk, uint32_t arr_id, int64_t i, uint32_t *res);
void obj_array_extend(struct workspace *wk, uint32_t a_id, uint32_t b_id);
bool obj_array_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res);
#endif
