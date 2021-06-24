#ifndef MUON_OBJECT_H
#define MUON_OBJECT_H

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
	obj_custom_target,
	obj_subproject,
	obj_dependency,
	obj_function,
	obj_feature_opt,
	obj_machine,
	obj_external_program,
	obj_external_library,
	obj_run_result,
	obj_configuration_data,
	obj_test,
	obj_module,

	obj_type_count,
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

enum dep_flags {
	dep_flag_found = 1 << 0,
	dep_flag_pkg_config = 1 << 1,
};

enum custom_target_flags {
	custom_target_capture = 1 << 0,
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
			uint32_t name;
			uint32_t build_name;
			uint32_t cwd;
			uint32_t build_dir;
			uint32_t src;
			uint32_t link_with;
			uint32_t include_directories;
			uint32_t deps;
			uint32_t c_args;
			enum tgt_type type;
		} tgt;
		struct {
			uint32_t name;
			uint32_t cmd;
			uint32_t args;
			uint32_t input;
			uint32_t output;
			uint32_t flags;
		} custom_target;
		struct {
			uint32_t name;
			uint32_t version;
			uint32_t link_with;
			uint32_t include_directories;
			uint32_t flags;
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
			bool found;
			uint32_t full_path;
		} external_library;
		struct {
			uint32_t out, err;
			int status;
		} run_result;
		struct {
			uint32_t dict;
		} configuration_data;
		struct {
			uint32_t name;
			uint32_t exe;
			uint32_t args;
		} test;
		uint32_t module;
	} dat;
};

const char *obj_type_to_s(enum obj_type t);
bool obj_to_s(struct workspace *wk, uint32_t id, char *buf, uint32_t len);
bool obj_equal(struct workspace *wk, uint32_t l, uint32_t r);
bool obj_clone(struct workspace *wk_src, struct workspace *wk_dest, uint32_t val, uint32_t *ret);

typedef enum iteration_result (*obj_array_iterator)(struct workspace *wk, void *ctx, uint32_t val);
void obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id);
bool obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb);
bool obj_array_foreach_flat(struct workspace *wk, uint32_t arr_id, void *usr_ctx, obj_array_iterator cb);
bool obj_array_in(struct workspace *wk, uint32_t l_id, uint32_t r_id, bool *res);
bool obj_array_index(struct workspace *wk, uint32_t arr_id, int64_t i, uint32_t *res);
void obj_array_extend(struct workspace *wk, uint32_t a_id, uint32_t b_id);
bool obj_array_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res);
bool obj_array_join(struct workspace *wk, uint32_t a_id, uint32_t join_id, uint32_t *obj);

typedef enum iteration_result (*obj_dict_iterator)(struct workspace *wk, void *ctx, uint32_t key, uint32_t val);
bool obj_dict_foreach(struct workspace *wk, uint32_t dict_id, void *ctx, obj_dict_iterator cb);
bool obj_dict_in(struct workspace *wk, uint32_t k_id, uint32_t dict_id, bool *res);
bool obj_dict_index(struct workspace *wk, uint32_t dict_id, uint32_t k_id, uint32_t *res, bool *found);
bool obj_dict_index_strn(struct workspace *wk, uint32_t dict_id, const char *key, uint32_t key_len, uint32_t *res, bool *found);
void obj_dict_set(struct workspace *wk, uint32_t dict_id, uint32_t key_id, uint32_t val_id);
bool obj_dict_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res);
#endif
