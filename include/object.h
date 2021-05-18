#ifndef BOSON_OBJECT_H
#define BOSON_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "iterator.h"

struct workspace;

enum obj_type {
	obj_any, // used for argument type checking
	obj_none,
	obj_string,
	obj_compiler,
	obj_meson,
	obj_array,
	obj_bool,
	obj_file,
	obj_build_target,
	obj_type_count,
};

struct obj {
	enum obj_type type;
	union {
		uint64_t n;
		const char *s;
		struct {
			uint32_t l;
			uint32_t r;
			uint32_t tail;
			uint32_t len;
			bool have_r;
		} arr;
		const char *f;
		struct {
			const char *name;
			uint32_t src;
			uint32_t include_directories;
		} tgt;
	} dat;
};


const char *obj_type_to_s(enum obj_type t);
void obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id);

typedef enum iteration_result (*obj_array_iterator)(struct workspace *wk, void *ctx, uint32_t val);
bool obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb);
#endif
