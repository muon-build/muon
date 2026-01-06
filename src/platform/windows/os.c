/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <stdio.h>
#include <windows.h>

#include "lang/string.h"
#include "platform/os.h"

bool
os_chdir(const char *path)
{
	BOOL res;

	res = SetCurrentDirectory(path);
	if (!res) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			errno = ENOENT;
		} else if (GetLastError() == ERROR_PATH_NOT_FOUND) {
			errno = ENOTDIR;
		} else if (GetLastError() == ERROR_FILENAME_EXCED_RANGE) {
			errno = ENAMETOOLONG;
		} else {
			errno = EIO;
		}
	}

	return res;
}

char *
os_getcwd(char *buf, size_t size)
{
	DWORD len;

	/* set errno to ERANGE for crossplatform usage of getcwd() in path.c */
	len = GetCurrentDirectory(0UL, NULL);
	if (size < len) {
		errno = ERANGE;
		return NULL;
	}

	len = GetCurrentDirectory(size, buf);
	if (!len) {
		errno = EPERM;
		return NULL;
	}

	return buf;
}

static uint32_t
count_bits(ULONG_PTR bit_mask)
{
	DWORD lshift;
	uint32_t bit_count = 0;
	ULONG_PTR bit_test;
	DWORD i;

	lshift = sizeof(ULONG_PTR) * 8 - 1;
	bit_test = (ULONG_PTR)1 << lshift;

	for (i = 0; i <= lshift; i++) {
		bit_count += ((bit_mask & bit_test) ? 1 : 0);
		bit_test /= 2;
	}

	return bit_count;
}

int32_t
os_ncpus(void)
{
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION iter;
	uint32_t ncpus;
	DWORD length;
	DWORD byte_offset;
	BOOL ret;

	buffer = NULL;
	length = 0UL;
	ret = GetLogicalProcessorInformation(buffer, &length);
	/*
	 * buffer and length values make this function failing
	 * with error being ERROR_INSUFFICIENT_BUFFER.
	 * Error not being ERROR_INSUFFICIENT_BUFFER is very unlikely.
	 */
	if (!ret) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			return -1;
		}
		/*
		 * Otherwise length is the size in bytes to allocate
		 */
	}

	buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(length);
	if (!buffer) {
		return -1;
	}

	ret = GetLogicalProcessorInformation(buffer, &length);
	/*
	 * Should not fail as buffer and length have the correct values,
	 * but anyway, we check the returned value.
	 */
	if (!ret) {
		free(buffer);
		return -1;
	}

	iter = buffer;
	byte_offset = 0;
	ncpus = 0;

	while (byte_offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= length) {
		switch (iter->Relationship) {
		case RelationProcessorCore: ncpus += count_bits(iter->ProcessorMask); break;
		default: break;
		}
		byte_offset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		iter++;
	}

	free(buffer);

	return ncpus;
}

void
os_set_env(struct workspace *wk, const struct str *k, const struct str *v)
{
	TSTR(buf_kv);

	tstr_pushn(wk, &buf_kv, k->s, k->len);
	tstr_push(wk, &buf_kv, '=');
	tstr_pushn(wk, &buf_kv, v->s, v->len);
	tstr_push(wk, &buf_kv, 0);

	putenv(buf_kv.buf);
}

bool
os_is_debugger_attached(void)
{
	return IsDebuggerPresent() == TRUE;
}

int32_t
os_get_pid(void)
{
	return GetCurrentProcessId();
}
