#ifndef MUON_LANG_OBJECT_H
#define MUON_LANG_OBJECT_H

#include <stdarg.h>
#include <stdio.h>

#include "compilers.h"
#include "iterator.h"
#include "lang/types.h"

enum obj_type {
	/* meta object types */
	obj_any = 0, // used for argument type checking
	obj_default, // used for function lookup

	/* singleton object types */
	obj_null,
	obj_meson,
	obj_disabler,
	obj_machine, // this won't be a singleton object when cross compilaton is implemented

	/* simple object types */
	obj_bool,
	obj_file,

	/* complex object types */
	_obj_aos_start,
	obj_number = _obj_aos_start,
	obj_string,
	obj_array,
	obj_dict,
	obj_compiler,
	obj_build_target,
	obj_custom_target,
	obj_subproject,
	obj_dependency,
	obj_feature_opt,
	obj_external_program,
	obj_external_library,
	obj_run_result,
	obj_configuration_data,
	obj_test,
	obj_module,
	obj_install_target,
	obj_environment,
	obj_include_directory,
	obj_option,
	obj_generator,
	obj_alias_target,

	obj_type_count,

	/* function argument typechecking object types. */
	ARG_TYPE_NULL     = 1000,
	ARG_TYPE_GLOB     = 1001,
	ARG_TYPE_ARRAY_OF = 1 << 20,
};

#if __STDC_VERSION__ >= 201112L
_Static_assert(ARG_TYPE_NULL > obj_type_count, "increase value of ARG_TYPE_NULL");
_Static_assert(!(ARG_TYPE_ARRAY_OF & ARG_TYPE_GLOB), "increase value of ARG_TYPE_GLOB");
#endif

enum tgt_type {
	tgt_executable = 1 << 0,
	tgt_static_library = 1 << 1,
	tgt_dynamic_library = 1 << 2,
	tgt_shared_module = 1 << 3,
};
enum tgt_type_count { tgt_type_count = 4, }; // keep in sync

enum feature_opt_state {
	feature_opt_auto,
	feature_opt_enabled,
	feature_opt_disabled,
};

enum module {
	module_fs,
	module_python,
	module_python3,
	module_pkgconfig,
	module_count,
};

enum build_option_type {
	op_string,
	op_boolean,
	op_combo,
	op_integer,
	op_array,
	op_feature,
	build_option_type_count,
};

enum str_flags {
	str_flag_big = 1 << 0,
};

struct str {
	const char *s;
	uint32_t len;
	enum str_flags flags;
};

enum build_tgt_flags {
	build_tgt_flag_export_dynamic = 1 << 0,
	build_tgt_flag_link_whole = 1 << 1,
	build_tgt_flag_pic = 1 << 2,
};

struct obj_internal {
	enum obj_type t;
	uint32_t val;
};

struct obj_subproject {
	uint32_t id;
	bool found;
};

struct obj_module {
	enum module module;
	bool found;
};

struct obj_array {
	obj val; // obj_any
	obj next; // obj_array
	obj tail; // obj_array
	uint32_t len;
	bool have_next;
};

struct obj_dict {
	obj key; // obj_string
	obj val; // obj_any
	obj next; // obj_array
	obj tail; // obj_array
	uint32_t len;
	bool have_next;
};

struct obj_build_target {
	obj name; // obj_string
	obj build_name; // obj_string
	obj cwd; // obj_string
	obj build_dir; // obj_string
	obj soname; // obj_string
	obj src; // obj_array
	obj link_with; // obj_array
	obj include_directories; // obj_array
	obj deps; // obj_array
	obj args; // obj_dict
	obj link_args; // obj_array

	enum build_tgt_flags flags;
	enum tgt_type type;
};

enum custom_target_flags {
	custom_target_capture = 1 << 0,
	custom_target_build_always_stale = 1 << 1,
};

struct obj_custom_target {
	obj name; // obj_string
	obj args; // obj_array
	obj input; // obj_array
	obj output; // obj_array
	obj depends; // obj_array
	enum custom_target_flags flags;
};

struct  obj_alias_target {
	obj name; // obj_string
	obj depends; // obj_array
};

enum dep_flags {
	dep_flag_found        = 1 << 0,
	dep_flag_pkg_config   = 1 << 1,
	// partial dependencies
	dep_flag_no_compile_args = 1 << 2,
	dep_flag_no_includes     = 1 << 3,
	dep_flag_no_link_args    = 1 << 4,
	dep_flag_no_links        = 1 << 5,
	dep_flag_no_sources      = 1 << 6,
	dep_flag_parts = dep_flag_no_compile_args
			 | dep_flag_no_includes
			 | dep_flag_no_link_args
			 | dep_flag_no_links
			 | dep_flag_no_sources,
};

enum include_type {
	include_type_preserve,
	include_type_system,
	include_type_non_system,
};

struct obj_dependency {
	obj name; // obj_string
	obj version; // obj_string
	obj link_with; // obj_array
	obj link_with_not_found; // obj_array
	obj link_args; // obj_array
	obj include_directories; // obj_array
	obj variables; // obj_dict
	obj sources; // obj_array
	obj deps; // obj_array
	obj compile_args; // obj_array
	enum dep_flags flags;
	enum include_type include_type;
};

struct obj_feature_opt {
	enum feature_opt_state state;
};

struct obj_external_program {
	bool found;
	obj full_path;
	obj ver;
};

struct obj_external_library {
	obj full_path;
	bool found;
};

struct obj_run_result {
	obj out;
	obj err;
	int32_t status;
};

struct obj_configuration_data {
	obj dict; // obj_dict
};

struct obj_test {
	obj name; // obj_string
	obj exe; // obj_string
	obj args; // obj_array
	obj env; // obj_array
	obj suites; // obj_array
	obj workdir; // obj_string
	bool should_fail;
};

struct obj_compiler {
	obj name;
	obj ver;
	obj libdirs;
	enum compiler_type type;
	enum compiler_language lang;
};

struct obj_install_target {
	obj src;
	obj dest;
	obj mode;
	bool build_target;
};

struct obj_environment {
	obj env; // dict
};

struct obj_include_directory {
	obj path;
	bool is_system;
};

struct obj_option {
	obj val;
	enum build_option_type type;
	obj choices;
	obj max;
	obj min;
};

struct obj_generator {
	obj output;
	obj raw_command;
	obj depfile;
	bool capture;
};

void make_obj(struct workspace *wk, obj *id, enum obj_type type);
enum obj_type get_obj_type(struct workspace *wk, obj id);

bool get_obj_bool(struct workspace *wk, obj o);
void set_obj_bool(struct workspace *wk, obj o, bool v);
int64_t get_obj_number(struct workspace *wk, obj o);
void set_obj_number(struct workspace *wk, obj o, int64_t v);
obj *get_obj_file(struct workspace *wk, obj o);
const char *get_file_path(struct workspace *wk, obj o);
const struct str *get_str(struct workspace *wk, obj s);

#define OBJ_GETTER(type) struct type *get_ ## type(struct workspace *wk, obj o)

OBJ_GETTER(obj_array);
OBJ_GETTER(obj_dict);
OBJ_GETTER(obj_compiler);
OBJ_GETTER(obj_build_target);
OBJ_GETTER(obj_custom_target);
OBJ_GETTER(obj_subproject);
OBJ_GETTER(obj_dependency);
OBJ_GETTER(obj_feature_opt);
OBJ_GETTER(obj_external_program);
OBJ_GETTER(obj_external_library);
OBJ_GETTER(obj_run_result);
OBJ_GETTER(obj_configuration_data);
OBJ_GETTER(obj_test);
OBJ_GETTER(obj_module);
OBJ_GETTER(obj_install_target);
OBJ_GETTER(obj_environment);
OBJ_GETTER(obj_include_directory);
OBJ_GETTER(obj_option);
OBJ_GETTER(obj_generator);
OBJ_GETTER(obj_alias_target);

#undef OBJ_GETTER

const char *obj_type_to_s(enum obj_type t);
void obj_to_s(struct workspace *wk, obj o, char *buf, uint32_t len);
bool obj_equal(struct workspace *wk, obj left, obj right);
bool obj_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val, obj *ret);

bool obj_vsnprintf(struct workspace *wk, char *out_buf, uint32_t buflen, const char *fmt, va_list ap)
__attribute__ ((format(printf, 4, 0)));
bool obj_snprintf(struct workspace *wk, char *out_buf, uint32_t buflen, const char *fmt, ...)
__attribute__ ((format(printf, 4, 5)));
bool obj_vfprintf(struct workspace *wk, FILE *f, const char *fmt, va_list ap)
__attribute__ ((format(printf, 3, 0)));
bool obj_fprintf(struct workspace *wk, FILE *f, const char *fmt, ...)
__attribute__ ((format(printf, 3, 4)));
bool obj_printf(struct workspace *wk, const char *fmt, ...)
__attribute__ ((format(printf, 2, 3)));

typedef enum iteration_result (*obj_array_iterator)(struct workspace *wk, void *ctx, obj val);
void obj_array_push(struct workspace *wk, obj arr, obj child);
bool obj_array_foreach(struct workspace *wk, obj arr, void *ctx, obj_array_iterator cb);
bool obj_array_foreach_flat(struct workspace *wk, obj arr, void *usr_ctx, obj_array_iterator cb);
bool obj_array_in(struct workspace *wk, obj arr, obj val);
void obj_array_index(struct workspace *wk, obj arr, int64_t i, obj *res);
void obj_array_extend(struct workspace *wk, obj arr, obj arr2);
void obj_array_dup(struct workspace *wk, obj arr, obj *res);
bool obj_array_join(struct workspace *wk, bool flat, obj arr, obj join, obj *res);
void obj_array_set(struct workspace *wk, obj arr, int64_t i, obj v);
void obj_array_dedup(struct workspace *wk, obj arr, obj *res);
void obj_array_flatten(struct workspace *wk, obj arr, obj *res);
bool obj_array_flatten_one(struct workspace *wk, obj val, obj *res);

typedef enum iteration_result (*obj_dict_iterator)(struct workspace *wk, void *ctx, obj key, obj val);
bool obj_dict_foreach(struct workspace *wk, obj dict, void *ctx, obj_dict_iterator cb);
bool obj_dict_in(struct workspace *wk, obj dict, obj key);
bool obj_dict_index(struct workspace *wk, obj dict, obj key, obj *res);
bool obj_dict_index_strn(struct workspace *wk, obj dict, const char *str, uint32_t len, obj *res);
void obj_dict_set(struct workspace *wk, obj dict, obj key, obj val);
void obj_dict_dup(struct workspace *wk, obj dict, obj *res);
void obj_dict_merge(struct workspace *wk, obj dict, obj dict2, obj *res);
void obj_dict_index_values(struct workspace *wk, obj dict, uint32_t i, obj *res);
void obj_dict_seti(struct workspace *wk, obj dict, uint32_t key, obj val);
bool obj_dict_geti(struct workspace *wk, obj dict, uint32_t key, obj *val);
#endif
