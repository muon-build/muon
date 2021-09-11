#ifndef MUON_LANG_OBJECT_H
#define MUON_LANG_OBJECT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "compilers.h"
#include "iterator.h"

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
	obj_feature_opt,
	obj_machine,
	obj_external_program,
	obj_external_library,
	obj_run_result,
	obj_configuration_data,
	obj_test,
	obj_module,
	obj_install_target,

	obj_type_count,

	ARG_TYPE_NULL     = 1000,
	ARG_TYPE_GLOB     = 1001,
	ARG_TYPE_ARRAY_OF = 1 << 20,
};

typedef uint32_t obj;

// this has to be included here so that obj_type and obj can be defined already
#include "lang/workspace.h"

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

enum module {
	module_fs,
	module_count,
};

struct obj {
	enum obj_type type;
	union {
		str str;
		str file;
		int64_t num;
		bool boolean;
		uint32_t subproj;
		enum module module;
		struct {
			obj val; // obj_any
			obj next; // obj_array
			obj tail; // obj_array
			uint32_t len;
			bool have_next;
		} arr;
		struct {
			obj key; // obj_string
			obj val; // obj_any
			obj next; // obj_array
			obj tail; // obj_array
			uint32_t len;
			bool have_next;
		} dict;
		struct {
			str name;
			str build_name;
			str cwd;
			str build_dir;
			obj src; // obj_array
			obj link_with; // obj_array
			obj include_directories; // obj_array
			obj deps; // obj_array
			obj args; // obj_dict
			obj link_args; // obj_array
			enum tgt_type type;
		} tgt;
		struct {
			str name;
			obj args; // obj_array
			obj input; // obj_array
			obj output; // obj_array
			enum custom_target_flags flags;
		} custom_target;
		struct {
			obj name; // obj_string
			obj version; // obj_string
			obj link_with; // obj_array
			obj link_args; // obj_array
			obj include_directories; // obj_array
			enum dep_flags flags;
		} dep;
		struct {
			enum feature_opt_state state;
		} feature_opt;
		struct {
			bool found;
			uint32_t full_path;
		} external_program;
		struct {
			str full_path;
			bool found;
		} external_library;
		struct {
			str out;
			str err;
			int32_t status;
		} run_result;
		struct {
			obj dict; // obj_dict
		} configuration_data;
		struct {
			obj name; // obj_string
			obj exe; // obj_string
			obj args; // obj_array
			bool should_fail;
		} test;
		struct {
			str name;
			str version;
			enum compiler_type type;
			enum compiler_language lang;
		} compiler;
		struct {
			str base_path;
			str filename;
			str install_dir;
			uint32_t install_mode;
		} install_target;
	} dat;
};

const char *obj_type_to_s(enum obj_type t);
bool obj_to_s(struct workspace *wk, uint32_t id, char *buf, uint32_t len);
bool obj_equal(struct workspace *wk, uint32_t l, uint32_t r);
bool obj_clone(struct workspace *wk_src, struct workspace *wk_dest, uint32_t val, uint32_t *ret);

bool obj_vsnprintf(struct workspace *wk, char *out_buf, uint32_t buflen, const char *fmt, va_list ap_orig)
__attribute__ ((format(printf, 4, 0)));
bool obj_snprintf(struct workspace *wk, char *out_buf, uint32_t buflen, const char *fmt, ...)
__attribute__ ((format(printf, 4, 5)));
bool obj_vfprintf(struct workspace *wk, FILE *f, const char *fmt, va_list ap)
__attribute__ ((format(printf, 3, 0)));
bool obj_fprintf(struct workspace *wk, FILE *f, const char *fmt, ...)
__attribute__ ((format(printf, 3, 4)));
bool obj_printf(struct workspace *wk, const char *fmt, ...)
__attribute__ ((format(printf, 2, 3)));

typedef enum iteration_result (*obj_array_iterator)(struct workspace *wk, void *ctx, uint32_t val);
void obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id);
bool obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb);
bool obj_array_foreach_flat(struct workspace *wk, uint32_t arr_id, void *usr_ctx, obj_array_iterator cb);
bool obj_array_index(struct workspace *wk, uint32_t arr_id, int64_t i, uint32_t *res);
void obj_array_extend(struct workspace *wk, uint32_t a_id, uint32_t b_id);
bool obj_array_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res);
bool obj_array_join(struct workspace *wk, uint32_t a_id, uint32_t join_id, uint32_t *obj);
bool obj_array_in(struct workspace *wk, obj arr, obj val);

typedef enum iteration_result (*obj_dict_iterator)(struct workspace *wk, void *ctx, uint32_t key, uint32_t val);
bool obj_dict_foreach(struct workspace *wk, uint32_t dict_id, void *ctx, obj_dict_iterator cb);
bool obj_dict_in(struct workspace *wk, uint32_t k_id, uint32_t dict_id);
bool obj_dict_index(struct workspace *wk, uint32_t dict_id, uint32_t k_id, uint32_t *res);
bool obj_dict_index_strn(struct workspace *wk, uint32_t dict_id, const char *key, uint32_t key_len, uint32_t *res);
void obj_dict_set(struct workspace *wk, uint32_t dict_id, uint32_t key_id, uint32_t val_id);
bool obj_dict_dup(struct workspace *wk, uint32_t arr_id, uint32_t *res);
bool obj_dict_merge(struct workspace *wk, uint32_t dict_a_id, uint32_t dict_b_id, uint32_t *res);
void obj_dict_seti(struct workspace *wk, uint32_t dict, uint32_t k, uint32_t v);
bool obj_dict_geti(struct workspace *wk, uint32_t dict, uint32_t k, uint32_t *v);
#endif
