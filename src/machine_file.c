/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "formats/ini.h"
#include "machine_file.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"

enum machine_file_section {
	machine_file_section_constants,
	machine_file_section_binaries,
	machine_file_section_properties,
	machine_file_section_builtin_options,
	machine_file_section_project_options,
	machine_file_section_project_options_prefixed,
	machine_file_section_build_machine,
	machine_file_section_host_machine,
	machine_file_section_count,
};

static const char *machine_file_section_names[machine_file_section_count] = {
	[machine_file_section_constants] = "constants",
	[machine_file_section_binaries] = "binaries",
	[machine_file_section_properties] = "properties",
	[machine_file_section_builtin_options] = "built-in options",
	[machine_file_section_project_options] = "project options",
	[machine_file_section_build_machine] = "build_machine",
	[machine_file_section_host_machine] = "host_machine",
};

static bool
machine_file_section_lookup(const char *val, const char *table[], uint32_t len, uint32_t *ret)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (table[i] && strcmp(val, table[i]) == 0) {
			*ret = i;
			return true;
		}
	}

	if (str_endswith(&STRL(val), &STR(":project options"))) {
		*ret = machine_file_section_project_options_prefixed;
		return true;
	}

	return false;
}

struct machine_file_translate_ctx {
	struct workspace *wk;
	struct tstr *dest;
	enum machine_kind machine;
};

static bool
machine_file_translate_cb(void *_ctx,
	struct source *src,
	const char *sect_str,
	const char *k,
	const char *v,
	struct source_location location)
{
	struct machine_file_translate_ctx *ctx = _ctx;
	struct workspace *wk = ctx->wk;
	enum machine_file_section sect;
	const char *native = ctx->machine == machine_kind_build ? "true" : "false";

	if (!k) {
		tstr_pushf(wk, ctx->dest, "\n# %s\n", sect_str);
		return true;
	}

	if (!sect_str) {
		error_messagef(wk, src, location, log_error, "key not under any section");
		return false;
	} else if (!machine_file_section_lookup(
			   sect_str, machine_file_section_names, machine_file_section_count, (uint32_t *)&sect)) {
		error_messagef(wk, src, location, log_error, "invalid section '%s'", sect_str);
		return false;
	}

	switch (sect) {
	case machine_file_section_constants: tstr_pushf(wk, ctx->dest, "%s = %s\n", k, v); break;
	case machine_file_section_binaries: {
		TSTR(key);
		TSTR(option_override);

		enum compiler_language l;
		if (s_to_compiler_language(k, &l)) {
			toolchain_component_option_name(wk, l, toolchain_component_compiler, ctx->machine, &option_override);
			tstr_pushf(wk, &key, "muon_%s_compiler", k);
		} else {
			tstr_pushs(wk, &key, k);
		}

		tstr_pushf(wk, ctx->dest, "meson.override_find_program('%s', [%s], native: %s)\n", key.buf, v, native);
		if (option_override.len) {
			tstr_pushf(wk, ctx->dest, "meson.set_option('%s', ['%s'])\n", option_override.buf, key.buf);
		}
		break;
	}
	case machine_file_section_properties:
		tstr_pushf(wk, ctx->dest, "meson.set_external_properties({'%s': %s}, native: %s)\n", k, v, native);
		break;
	case machine_file_section_host_machine:
		tstr_pushf(wk, ctx->dest, "host_machine.set_properties({'%s': %s})\n", k, v);
		break;
	case machine_file_section_build_machine:
		tstr_pushf(wk, ctx->dest, "build_machine.set_properties({'%s': %s})\n", k, v);
		break;
	case machine_file_section_builtin_options:
	case machine_file_section_project_options:
		tstr_pushf(wk, ctx->dest, "meson.set_option(%s, %s)\n", k, v);
		break;
	case machine_file_section_project_options_prefixed:
		tstr_pushf(wk, ctx->dest, "# TODO: option needs prefix: meson.set_option(%s, %s)\n", k, v);
		break;
	case machine_file_section_count: UNREACHABLE;
	}

	return true;
}

static bool
machine_file_translate(struct workspace *wk, struct tstr *dest, const char *path, enum machine_kind machine)
{
	struct machine_file_translate_ctx ctx = {
		.wk = wk,
		.dest = dest,
		.machine = machine,
	};

	tstr_pushf(wk, ctx.dest, "# auto generated from: %s\n", path);

	struct source src;
	char *buf = 0;
	if (!ini_parse(wk, path, &src, &buf, machine_file_translate_cb, &ctx)) {
		return false;
	}

	return true;
}

bool
machine_file_eval(struct workspace *wk, const char *path, enum machine_kind machine)
{
	TSTR(translated);

	if (!machine_file_translate(wk, &translated, path, machine)) {
		return false;
	}

	L("translated machine file:\n%s", translated.buf);

	stack_push(&wk->stack, wk->vm.lang_mode, language_extended);
	stack_push(&wk->stack, wk->vm.scope_stack, wk->vm.behavior.scope_stack_dup(wk, wk->vm.default_scope_stack));

	TSTR(translated_label);
	tstr_pushf(wk, &translated_label, "%s-translated", path);

	struct source translated_src = { .src = translated.buf, .len = translated.len, .label = translated_label.buf };

	obj res;
	if (!eval(wk, &translated_src, build_language_meson, 0, &res)) {
		return false;
	}

	stack_pop(&wk->stack, wk->vm.scope_stack);
	stack_pop(&wk->stack, wk->vm.lang_mode);

	return true;
}
