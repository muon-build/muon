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
	obj_dict,
	obj_bool,
	obj_file,
	obj_build_target,
	obj_subproject,
	obj_dependency,
	obj_type_count,
	obj_function,
	obj_feature_opt,
	obj_machine,
	obj_external_program,
	obj_run_result,
};

enum tgt_type {
	tgt_executable,
	tgt_library
};

enum feature_opt_state {
	feature_opt_auto,
	feature_opt_enabled,
	feature_opt_disabled,
};

struct obj {
	enum obj_type type;
	union {
		uint64_t n;
		uint32_t str;
		int64_t num;
		bool boolean;
		struct {
			uint32_t l; // value
			uint32_t r; // tail
			uint32_t tail;
			uint32_t len;
			bool have_r;
		} arr;
		struct {
			uint32_t key;
			uint32_t l; // value
			uint32_t r; // tail
			uint32_t tail;
			uint32_t len;
			bool have_r;
		} dict;
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
			bool found;
		} dep;
		struct {
			uint32_t def;
			uint32_t args;
			uint32_t body;
		} func;
		uint32_t subproj;
		struct {
			enum feature_opt_state state;
		} feature_opt;
		struct {
			bool found;
			uint32_t full_path;
		} external_program;
		struct {
			uint32_t out, err;
			int status;
		} run_result;
	} dat;
};


const char *obj_type_to_s(enum obj_type t);
void obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id);

bool obj_equal(struct workspace *wk, uint32_t l, uint32_t r);

typedef enum iteration_result (*obj_array_iterator)(struct workspace *wk, void *ctx, uint32_t val);
bool obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb);
bool obj_array_in(struct workspace *wk, uint32_t l_id, uint32_t r_id, bool *res);
bool obj_array_index(struct workspace *wk, uint32_t arr_id, int64_t i, uint32_t *res);
void obj_array_extend(struct workspace *wk, uint32_t a_id, uint32_t b_id);
bool obj_array_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res);


typedef enum iteration_result (*obj_dict_iterator)(struct workspace *wk, void *ctx, uint32_t key, uint32_t val);
bool obj_dict_foreach(struct workspace *wk, uint32_t dict_id, void *ctx, obj_dict_iterator cb);
bool obj_dict_in(struct workspace *wk, uint32_t k_id, uint32_t dict_id, bool *res);
bool obj_dict_index(struct workspace *wk, uint32_t dict_id, uint32_t k_id, uint32_t *res, bool *found);
void obj_dict_set(struct workspace *wk, uint32_t dict_id, uint32_t key_id, uint32_t val_id);
#endif
