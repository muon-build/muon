#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/machine.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/uname.h"

static bool
func_machine_system(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *sysname;
	if (!uname_sysname(&sysname)) {
		return false;
	}

	const char *map[][2] = {
		{ "Linux", "linux" },
		{ "Darwin", "darwin" },
		0
	};

	uint32_t i;
	for (i = 0; map[i][0]; ++i) {
		if (strcmp(map[i][0], sysname) == 0) {
			*res = make_str(wk, map[i][1]);
			return true;
		}
	}

	LOG_E("unknown sysname '%s'", sysname);
	return false;
}

static bool
func_machine_endian(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	enum endianness e;
	if (!uname_endian(&e)) {
		return false;
	}

	const char *s = NULL;
	switch (e) {
	case little_endian:
		s = "little";
		break;
	case big_endian:
		s = "big";
		break;
	}

	*res = make_str(wk, s);
	return true;
}

static bool
func_machine_cpu_family(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *machine;
	if (!uname_machine(&machine)) {
		return false;
	}

	const char *map[][2] = {
		{ "x86_64", "x86_64" },
		0
	};

	uint32_t i;
	for (i = 0; map[i][0]; ++i) {
		if (strcmp(map[i][0], machine) == 0) {
			*res = make_str(wk, map[i][1]);
			return true;
		}
	}

	LOG_E("unknown cpu '%s'", machine);
	return false;
}

static bool
func_machine_cpu(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *machine;
	if (!uname_machine(&machine)) {
		return false;
	}

	*res = make_str(wk, machine);
	return true;
}

const struct func_impl_name impl_tbl_machine[] = {
	{ "cpu", func_machine_cpu },
	{ "cpu_family", func_machine_cpu_family },
	{ "endian", func_machine_endian },
	{ "system", func_machine_system },
	{ NULL, NULL },
};
