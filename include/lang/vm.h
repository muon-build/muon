/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_VM_H
#define MUON_LANG_VM_H

#include "datastructures/arr.h"
#include "datastructures/bucket_arr.h"
#include "datastructures/hash.h"
#include "lang/object.h"
#include "lang/source.h"
#include "lang/types.h"

enum op {
	op_constant = 1,
	op_constant_list,
	op_constant_dict,
	op_constant_func,
	op_add,
	op_sub,
	op_mul,
	op_div,
	op_mod,
	op_not,
	op_eq,
	op_in,
	op_gt,
	op_lt,
	op_negate,
	op_stringify,
	op_store,
	op_add_store,
	op_load,
	op_try_load,
	op_return,
	op_return_end,
	op_call,
	op_call_method,
	op_call_native,
	op_index,
	op_iterator,
	op_iterator_next,
	op_jmp,
	op_jmp_if_true,
	op_jmp_if_false,
	op_jmp_if_disabler,
	op_jmp_if_disabler_keep,
	op_pop,
	op_dup,
	op_swap,
	op_typecheck,
	// Analyzer only ops
	op_az_branch,
	op_az_merge,

	op_count,
};

struct workspace;

enum variable_assignment_mode {
	assign_local,
	assign_reassign,
};

enum compile_time_constant_objects {
	disabler_id = 1,
	obj_bool_true = 2,
	obj_bool_false = 3,
};

struct obj_stack_entry {
	obj o;
	uint32_t ip;
};

struct object_stack {
	struct bucket_arr ba;
	struct obj_stack_entry *page;
	uint32_t i, bucket;
};

struct source_location_mapping {
	struct source_location loc;
	uint32_t src_idx, ip;
};

enum call_frame_type {
	call_frame_type_eval,
	call_frame_type_func,
};

struct call_frame {
	type_tag expected_return_type;
	enum call_frame_type type;
	obj scope_stack;
	uint32_t return_ip, call_stack_base;
	enum language_mode lang_mode;
};

struct vm_compiler_state {
	struct bucket_arr nodes;
	struct arr node_stack;
	struct arr loop_jmp_stack, if_jmp_stack;
	bool err;
};

struct vm_dbg_state {
	uint32_t node, last_line;
	bool stepping, break_on_err, dump_signature;
	obj watched;
	obj eval_trace;
	bool eval_trace_subdir;
};

struct vm_behavior {
	void((*assign_variable)(struct workspace *wk,
		const char *name,
		obj o,
		uint32_t n_id,
		enum variable_assignment_mode mode));
	void((*unassign_variable)(struct workspace *wk, const char *name));
	void((*push_local_scope)(struct workspace *wk));
	void((*pop_local_scope)(struct workspace *wk));
	obj((*scope_stack_dup)(struct workspace *wk, obj scope_stack));
	bool((*get_variable)(struct workspace *wk, const char *name, obj *res));
	bool((*eval_project_file)(struct workspace *wk, const char *path, bool first));
	bool((*native_func_dispatch)(struct workspace *wk, uint32_t func_idx, obj self, obj *res));
	bool((*pop_args)(struct workspace *wk, struct args_norm an[], struct args_kw akw[]));
	bool((*func_lookup)(struct workspace *wk, obj self, const char *name, uint32_t *idx, obj *func));
};

struct vm_objects {
	struct bucket_arr chrs;
	struct bucket_arr objs;
	struct bucket_arr dict_elems, dict_hashes;
	struct bucket_arr obj_aos[obj_type_count - _obj_aos_start];
	struct hash obj_hash, str_hash;
	bool obj_clear_mark_set;
};

typedef void((*vm_op_fn)(struct workspace *wk));
struct vm_ops {
	vm_op_fn ops[op_count];
};

struct vm {
	struct object_stack stack;
	struct arr call_stack, locations, code, src;
	uint32_t ip, nargs, nkwargs;
	obj scope_stack, default_scope_stack;

	struct vm_ops ops;
	struct vm_objects objects;
	struct vm_behavior behavior;
	struct vm_compiler_state compiler_state;
	struct vm_dbg_state dbg_state;

	enum language_mode lang_mode;

	bool run;
	bool saw_disabler;
	bool in_analyzer;
	// When true, disable functions with the .fuzz_unsafe attribute set to true.
	// This is useful when running `muon internal eval` on randomly generated
	// files, where you don't want to accidentally execute `run_command('rm',
	// '-rf', '/')` for example
	bool disable_fuzz_unsafe_functions;
	bool error;
};

void object_stack_push(struct workspace *wk, obj o);
struct obj_stack_entry *object_stack_pop_entry(struct object_stack *s);
void object_stack_discard(struct object_stack *s, uint32_t n);
void object_stack_print(struct workspace *wk, struct object_stack *s);

obj vm_get_constant(uint8_t *code, uint32_t *ip);
obj vm_execute(struct workspace *wk);
bool
vm_eval_capture(struct workspace *wk, obj capture, const struct args_norm an[], const struct args_kw akw[], obj *res);
void vm_lookup_inst_location_src_idx(struct vm *vm, uint32_t ip, struct source_location *loc, uint32_t *src_idx);
void vm_dis(struct workspace *wk);
void vm_init(struct workspace *wk);
void vm_init_objects(struct workspace *wk);
void vm_destroy(struct workspace *wk);
void vm_destroy_objects(struct workspace *wk);

bool pop_args(struct workspace *wk, struct args_norm an[], struct args_kw akw[]);
bool vm_pop_args(struct workspace *wk, struct args_norm an[], struct args_kw akw[]);
void vm_op_return(struct workspace *wk);

MUON_ATTR_FORMAT(printf, 4, 5)
void vm_diagnostic(struct workspace *wk, uint32_t ip, enum log_level lvl, const char *fmt, ...);
MUON_ATTR_FORMAT(printf, 3, 4) void vm_error_at(struct workspace *wk, uint32_t ip, const char *fmt, ...);
MUON_ATTR_FORMAT(printf, 2, 3) void vm_error(struct workspace *wk, const char *fmt, ...);
MUON_ATTR_FORMAT(printf, 3, 4) void vm_warning_at(struct workspace *wk, uint32_t ip, const char *fmt, ...);
MUON_ATTR_FORMAT(printf, 2, 3) void vm_warning(struct workspace *wk, const char *fmt, ...);
#endif
