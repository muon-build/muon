/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_INTERPRETER_H
#define MUON_LANG_INTERPRETER_H

#include "compat.h"

#include <stdbool.h>
#include <stddef.h>

#include "object.h"
#include "parser.h"
#include "workspace.h"

bool interp_node(struct workspace *wk, uint32_t n_id, obj *res);
bool interp_arithmetic(struct workspace *wk, uint32_t err_node,
	enum arithmetic_type type, bool plusassign, uint32_t nl, uint32_t nr,
	obj *res);
bool interp_index(struct workspace *wk, struct node *n, obj l_id, bool do_chain, obj *res);
bool interp_stringify(struct workspace *wk, struct node *n, obj *res);
bool interp_comparison(struct workspace *wk, struct node *n, obj *res);

void interp_error(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
MUON_ATTR_FORMAT(printf, 3, 4);
void interp_warning(struct workspace *wk, uint32_t n_id, const char *fmt, ...)
MUON_ATTR_FORMAT(printf, 3, 4);

bool typecheck_array(struct workspace *wk, uint32_t n_id, obj arr, type_tag type);
bool typecheck_dict(struct workspace *wk, uint32_t n_id, obj dict, type_tag type);

bool boundscheck(struct workspace *wk, uint32_t n_id, uint32_t len, int64_t *i);
bool bounds_adjust(struct workspace *wk, uint32_t len, int64_t *i);
bool rangecheck(struct workspace *wk, uint32_t n_id, int64_t min, int64_t max, int64_t n);

void assign_variable(struct workspace *wk, const char *name, obj o, uint32_t _n_id, enum variable_assignment_mode mode);
void unassign_variable(struct workspace *wk, const char *name);
void push_local_scope(struct workspace *wk);
void pop_local_scope(struct workspace *wk);
bool get_variable(struct workspace *wk, const char *name, obj *res, uint32_t proj_id);
obj scope_stack_dup(struct workspace *wk, obj scope_stack);

void interpreter_init(void);
#endif
