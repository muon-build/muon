/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <windows.h>
#include <winioctl.h>

#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/os.h"
#include "platform/path.h"
#include "platform/windows/log.h"
#include "platform/windows/win32_error.h"

bool tty_is_pty = true;

bool
fs_exists(const char *path)
{
	HANDLE h;

	h = CreateFile(path,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_ARCHIVE | FILE_FLAG_BACKUP_SEMANTICS,
		NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}

	CloseHandle(h);

	return true;
}

bool
fs_symlink_exists(const char *path)
{
	return false;
	(void)path;
}

bool
fs_file_exists(const char *path)
{
	BY_HANDLE_FILE_INFORMATION fi;
	HANDLE h;

	h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_ARCHIVE, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}

	if (!GetFileInformationByHandle(h, &fi)) {
		return false;
	}

	CloseHandle(h);

	return (fi.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) == FILE_ATTRIBUTE_ARCHIVE;
}

// https://github.com/winsiderss/systeminformer/blob/f0c0366050633d89a1fae805c7a5f344c410fbf0/phnt/include/ntioapi.h#L2881
typedef struct _REPARSE_DATA_BUFFER {
	DWORD ReparseTag;
	USHORT ReparseDataLength;
	USHORT Reserved;
	union {
		struct {
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			ULONG Flags;
			WCHAR PathBuffer[1];
		} SymbolicLinkReparseBuffer;
		struct {
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPointReparseBuffer;
		struct {
			ULONG StringCount;
			WCHAR StringList[1];
		} AppExecLinkReparseBuffer;
	} u;
} REPARSE_DATA_BUFFER;

static char *
fs_resolve_reparse_point(struct workspace *wk, const char *path)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	REPARSE_DATA_BUFFER *buf = 0;
	DWORD size;
	const wchar_t *str = 0;
	const wchar_t *wide_path = 0;
	char *resolved;
	DWORD resolved_size;
	DWORD wide_ret;

	h = CreateFile(path,
		FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		0,
		OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT,
		0);
	if (h == INVALID_HANDLE_VALUE) {
		return 0;
	}

	resolved = 0;

	size = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
	buf = ar_alloc(wk->a_scratch, size, 1, 1);

	if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, 0, 0, buf, size, &size, 0)) {
		LOG_E("DeviceIoControl failed to get reparse point: %s", win32_error());
		goto done;
	}

	if (!IsReparseTagMicrosoft(buf->ReparseTag)) {
		goto done;
	}

	switch (buf->ReparseTag) {
	case IO_REPARSE_TAG_APPEXECLINK: {
		str = &buf->u.AppExecLinkReparseBuffer.StringList[0];
		for (ULONG i = 0; i < buf->u.AppExecLinkReparseBuffer.StringCount; i++) {
			size = wcslen(str);
			if (path_wide_begins_with_win32_drive(str)) {
				wide_path = str;
				break;
			}
			str += size + 1;
		}
		break;
	}
	}

	if (wide_path) {
		resolved_size = size + 1;
		resolved = ar_alloc(wk->a_scratch, resolved_size, 1, 1);

convert:
		if (!resolved) {
			goto done;
		}

		if (!(wide_ret = WideCharToMultiByte(CP_UTF8, 0, wide_path, size, resolved, resolved_size, 0, 0))) {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				DWORD new_resolved_size = resolved_size * 2;
				resolved = ar_realloc(wk->a_scratch, resolved, resolved_size, new_resolved_size, 1);
				resolved_size = new_resolved_size;
				goto convert;
			}
			goto done;
		}

		resolved[wide_ret] = 0;
	}

done:
	if (h != INVALID_HANDLE_VALUE) {
		CloseHandle(h);
	}
	return resolved;
}

bool
fs_exe_exists(struct workspace *wk, const char *path)
{
	HANDLE h;
	HANDLE fm;
	unsigned char *base;
	unsigned char *iter;
	int64_t size;
	DWORD size_high;
	DWORD size_low;
	DWORD offset;
	bool ret = false;
	bool is_reparse = false;

open_file:
	h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		if (!is_reparse && GetLastError() == ERROR_CANT_ACCESS_FILE) {
			path = fs_resolve_reparse_point(wk, path);

			if (path) {
				is_reparse = true;
				goto open_file;
			}
		}

		return ret;
	}

	size_low = GetFileSize(h, &size_high);
	if ((size_low == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR)) {
		goto close_file;
	}

	size = size_low | ((int64_t)size_high << 32);

	/*
	 * PE file is organized as followed:
	 *  1) MS-DOS header (60 bytes) (beginning with bytes 'M' then 'Z')
	 *  2) offset of PE signature (4 bytes)"
	 *  2) PE signature (4 bytes) : "PE\0\0"
	 *  3) COFF File Header (20 bytes)
	 * the rest is useless for us.
	 */
	if (size < 64) {
		/* LOG_I("file %s is too small", path); */
		goto close_file;
	}

	fm = CreateFileMapping(h, NULL, PAGE_READONLY, 0UL, 0UL, NULL);
	if (!fm) {
		/* LOG_I("Can not map file: %s", win32_error()); */
		goto close_file;
	}

	base = MapViewOfFile(fm, FILE_MAP_READ, 0, 0, 0);
	if (!base) {
		/* LOG_I("Can not view map: %s", win32_error()); */
		goto close_fm;
	}

	iter = base;

	if (*((WORD *)iter) != 0x5a4d) {
		/* LOG_I("file %s is not a MS-DOS file", path); */
		goto unmap;
	}

	offset = *((DWORD *)(iter + 0x3c));
	if (size < offset + 24) {
		/* LOG_I("file %s is too small", path); */
		goto unmap;
	}

	iter += offset;
	if ((iter[0] != 'P') && (iter[1] != 'E') && (iter[2] != '\0') && (iter[3] != '\0')) {
		/* LOG_I("file %s is not a PE file", path); */
		goto unmap;
	}

	iter += 22;
	if ((!((*((WORD *)iter)) & 0x0002)) || ((*((WORD *)iter)) & 0x2000)) {
		/* LOG_I("file %s is not a binary file", path); */
		goto unmap;
	}

	ret = true;

unmap:
	UnmapViewOfFile(base);
close_fm:
	CloseHandle(fm);
close_file:
	CloseHandle(h);

	return ret;
}

bool
fs_dir_exists(const char *path)
{
	BY_HANDLE_FILE_INFORMATION fi;
	HANDLE h;

	h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}

	if (!GetFileInformationByHandle(h, &fi)) {
		return false;
	}

	CloseHandle(h);

	return (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
}

bool
fs_mkdir(const char *path, bool exist_ok)
{
	if (!CreateDirectory(path, NULL)) {
		if (exist_ok && GetLastError() == ERROR_ALREADY_EXISTS) {
			return true;
		}
		LOG_E("failed to create directory \"%s\": %s", path, win32_error());
		return false;
	}

	return true;
}

bool
fs_rmdir(const char *path, bool force)
{
	if (!RemoveDirectory(path)) {
		if (force) {
			return true;
		}

		LOG_E("failed to remove directory %s: %s\n", path, win32_error());
		return false;
	}

	return true;
}

bool
fs_copy_file(struct workspace *wk, const char *src, const char *dest, bool force)
{
	if (force) {
		fs_make_writeable_if_exists(dest);
	}

	if (!CopyFile(src, dest, FALSE)) {
		LOG_E("failed to copy file %s: %s", src, win32_error());
		return false;
	}

	return true;
}

bool
fs_dir_foreach(struct workspace *wk, const char *path, void *_ctx, fs_dir_foreach_cb cb)
{
	HANDLE h;
	char *filter;
	WIN32_FIND_DATA fd;
	size_t len;
	bool loop = true, res = true;

	if (!path || !*path) {
		return false;
	}

	len = strlen(path);
	if ((path[len - 1] == '/') || (path[len - 1] == '\\')) {
		len--;
	}

	filter = ar_alloc(wk->a_scratch, len + 3, 1, 1);

	CopyMemory(filter, path, len);
	filter[len] = '\\';
	filter[len + 1] = '*';
	filter[len + 2] = '\0';

	h = FindFirstFileEx(filter, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
	if (h == INVALID_HANDLE_VALUE) {
		LOG_E("failed to open directory %s: %s", path, win32_error());
		return false;
	}

	do {
		if (fd.cFileName[0] == '.') {
			if (fd.cFileName[1] == '\0') {
				continue;
			} else {
				if ((fd.cFileName[1] == '.') && (fd.cFileName[2] == '\0')) {
					continue;
				}
			}
		}

		switch (cb(_ctx, fd.cFileName)) {
		case ir_cont: break;
		case ir_done: loop = false; break;
		case ir_err:
			loop = false;
			res = false;
			break;
		}
	} while (loop && FindNextFile(h, &fd));

	if (!FindClose(h)) {
		LOG_E("failed to close handle: %s", win32_error());
		res = false;
	}

	return res;
}

bool
fs_make_symlink(const char *target, const char *path, bool force)
{
	return false;
	(void)target;
	(void)path;
	(void)force;
}

const char *
fs_user_home(void)
{
	return os_get_env("USERPROFILE");
}

static inline bool
_is_wprefix(const WCHAR *s, const WCHAR *prefix, uint32_t n)
{
	return wcsncmp(s, prefix, n) == 0;
}

#define is_wprefix(__s, __p) _is_wprefix(__s, __p, sizeof(__p) / sizeof(WCHAR) - 1)

bool
fs_is_a_tty_from_fd(struct workspace *wk, int fd)
{
	HANDLE h;
	DWORD mode;
	bool ret = false;

	h = (HANDLE *)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		return ret;
	}

	/* first, check if the handle is assocoated with a conpty-based terminal */
	{
		HMODULE mod;

		mod = LoadLibrary("kernel32.dll");
		if (mod) {
			if (GetProcAddress(mod, "ClosePseudoConsole")) {
				if (GetConsoleMode(h, &mode)) {
					// ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT
					mode |= 0x4 | 0x1;
					if (SetConsoleMode(h, mode)) {
						FreeLibrary(mod);
						tty_is_pty = true;
						return true;
					}
				}
			}
			FreeLibrary(mod);
		}
	}

	/*
	 * test if the stream is associated to a mintty-based terminal
	 * based on:
	 * https://fossies.org/linux/vim/src/iscygpty.c
	 * https://sourceforge.net/p/mingw-w64/mailman/message/35589741/
	 * it means mintty without conpty
	 */
	{
		if (GetFileType(h) == FILE_TYPE_PIPE) {
			FILE_NAME_INFO *fni;
			WCHAR *p = NULL;
			size_t size;
			size_t l;

			size = sizeof(FILE_NAME_INFO) + sizeof(WCHAR) * (MAX_PATH - 1);
			fni = ar_alloc(wk->a_scratch, size + sizeof(WCHAR), 1, 1);

			if (GetFileInformationByHandleEx(h, FileNameInfo, fni, size)) {
				fni->FileName[fni->FileNameLength / sizeof(WCHAR)] = L'\0';
				/*
				 * Check the name of the pipe:
				 * '\{cygwin,msys}-XXXXXXXXXXXXXXXX-ptyN-{from,to}-master'
				 */
				p = fni->FileName;
				if (is_wprefix(p, L"\\cygwin-")) {
					p += 8;
				} else if (is_wprefix(p, L"\\msys-")) {
					p += 6;
				} else {
					p = NULL;
				}
				if (p) {
					/* Skip 16-digit hexadecimal. */
					if (wcsspn(p, L"0123456789abcdefABCDEF") == 16) {
						p += 16;
					} else {
						p = NULL;
					}
				}
				if (p) {
					if (is_wprefix(p, L"-pty")) {
						p += 4;
					} else {
						p = NULL;
					}
				}
				if (p) {
					/* Skip pty number. */
					l = wcsspn(p, L"0123456789");
					if (l >= 1 && l <= 4) {
						p += l;
					} else {
						p = NULL;
					}
					if (p) {
						if (is_wprefix(p, L"-from-master")) {
							//p += 12;
						} else if (is_wprefix(p, L"-to-master")) {
							//p += 10;
						} else {
							p = NULL;
						}
					}
				}
			}
			if (p) {
				tty_is_pty = true;
				return true;
			}
		}
	}

	/*
	 * last case: cmd without conpty
	 */
	if (GetConsoleMode(h, &mode)) {
		tty_is_pty = false;
		return true;
	}

	return false;
}

bool
fs_chmod(const char *path, uint32_t mode)
{
	int mask = _S_IREAD;

	if (mode & _S_IWRITE) {
		mask |= _S_IWRITE;
	}

	if (_chmod(path, mask) == -1) {
		LOG_E("failed chmod(%s, %o): %s", path, mode, strerror(errno));
		return false;
	}

	return true;
}

bool
fs_has_extension(const char *path, const char *ext)
{
	char *s;

	s = strrchr(path, '.');
	if (!s) {
		return false;
	}

	return lstrcmpi(s, ext) == 0;
}

bool
fs_find_cmd(struct workspace *wk, struct tstr *buf, const char *cmd)
{
	assert(*cmd);
	uint32_t len;
	const char *env_path, *base_start;

	tstr_clear(buf);

	if (!path_is_basename(cmd)) {
		path_make_absolute(wk, buf, cmd);

		if (fs_exe_exists(wk, buf->buf)) {
			return true;
		}

		if (!fs_has_extension(buf->buf, ".exe")) {
			tstr_pushs(wk, buf, ".exe");
			if (fs_exe_exists(wk, buf->buf)) {
				return true;
			}
		}

		return false;
	}

	if (strcmp(cmd, "cmd") == 0 || strcmp(cmd, "cmd.exe") == 0) {
		tstr_pushs(wk, buf, "cmd.exe");
		return true;
	}

	if (!(env_path = os_get_env("PATH"))) {
		LOG_E("failed to get the value of PATH");
		return false;
	}

	base_start = env_path;
	while (true) {
		if (!*env_path || *env_path == ';') {
			len = env_path - base_start;

			tstr_clear(buf);
			tstr_pushn(wk, buf, base_start, len);

			base_start = env_path + 1;

			path_push(wk, buf, cmd);

			if (fs_exe_exists(wk, buf->buf)) {
				return true;
			} else if (!fs_has_extension(buf->buf, ".exe")) {
				tstr_pushs(wk, buf, ".exe");
				if (fs_exe_exists(wk, buf->buf)) {
					return true;
				}
			}

			if (!*env_path) {
				break;
			}
		}

		++env_path;
	}

	return false;
}

enum fs_mtime_result
fs_mtime(const char *path, int64_t *mtime)
{
	WIN32_FILE_ATTRIBUTE_DATA d;
	ULARGE_INTEGER t;
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &d)) {
		return fs_mtime_result_not_found;
	}

	t.LowPart = d.ftLastWriteTime.dwLowDateTime;
	t.HighPart = d.ftLastWriteTime.dwHighDateTime;
	*mtime = t.QuadPart / 100;
	return fs_mtime_result_ok;
}

bool
fs_remove(const char *path)
{
	bool ok = DeleteFileA(path);
	if (!ok) {
		if (GetLastError() == ERROR_ACCESS_DENIED) {
			if (!fs_chmod(path, _S_IWRITE)) {
				return false;
			}
			ok = DeleteFileA(path);
		}
	}

	if (!ok) {
		LOG_E("failed DeleteFile(\"%s\"): %s", path, win32_error());
		return false;
	}
	return true;
}

FILE *
fs_make_tmp_file(const char *name, const char *suffix, char *buf, uint32_t len)
{
	static uint32_t unique = 0;
	++unique;

	char tmp_dir[MAX_PATH + 1];
	DWORD result = GetTempPath(sizeof(tmp_dir), tmp_dir);
	if (result == 0) {
		// Use '.' (current dir) as tmp dir if GetTempPath fails
		tmp_dir[0] = '.';
		tmp_dir[1] = 0;
	}

	snprintf(buf, len, "%s\\__muon_tmp_%d_%s.%s", tmp_dir, unique, name, suffix);

	return fs_fopen(buf, "w+b");
}

bool
fs_wait_for_input(int fd, uint32_t *bytes_available)
{
	intptr_t _h = _get_osfhandle(fd);
	if (_h == -2 || (HANDLE)_h == INVALID_HANDLE_VALUE) {
		LOG_E("failed _get_osfhandle(): %s", win32_error());
		return false;
	}
	HANDLE h = (HANDLE)_h;
	switch (GetFileType(h)) {
	case FILE_TYPE_CHAR: {
		LOG_E("FILE_TYPE_CHAR not supported");
		return false;
	}
	case FILE_TYPE_PIPE: {
		while (true) {
			DWORD dwBytesAvailable;
			if (!PeekNamedPipe(h, NULL, 0, NULL, &dwBytesAvailable, NULL)) {
				LOG_E("PeekNamedPipe: %s", win32_error());
				return false;
			}

			if (dwBytesAvailable) {
				*bytes_available = dwBytesAvailable;
				break;
			}

			if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0) {
				LOG_E("failed WaitForSingleObject(0x%p): %s", h, win32_error());
				return false;
			}
		}
		break;
	}
	case FILE_TYPE_DISK: {
		return true;
	}
	}

	return true;
}
