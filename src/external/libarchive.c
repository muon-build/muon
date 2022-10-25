/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <archive.h>
#include <archive_entry.h>

#include "buf_size.h"
#include "external/libarchive.h"
#include "lang/string.h"
#include "log.h"
#include "platform/path.h"

const bool have_libarchive = true;

static int
copy_data(struct archive *ar, struct archive *aw)
{
	int r;
	const void *buff;
	size_t size;
	la_int64_t offset;

	while (true) {
		if ((r = archive_read_data_block(ar, &buff, &size, &offset)) == ARCHIVE_EOF) {
			return ARCHIVE_OK;
		} else if (r < ARCHIVE_OK) {
			return r;
		}

		if ((r = archive_write_data_block(aw, buff, size, offset)) < ARCHIVE_OK) {
			LOG_E("error writing archive data block: %s\n", archive_error_string(aw));
			return r;
		}
	}
}

bool
muon_archive_extract(const char *buf, size_t size, const char *dest_path)
{
	bool res = false;
	struct archive *a;
	struct archive *ext;
	struct archive_entry *entry;
	int flags;
	int r;

	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	a = archive_read_new();
	archive_read_support_format_all(a);
	archive_read_support_filter_all(a);
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);

	if ((r = archive_read_open_memory(a, buf, size))) {
		// may not work, a might not be initialized ??
		LOG_E("error opening archive: %s\n", archive_error_string(a));
		goto ret;
	}

	SBUF_manual(path);

	while (true) {
		if ((r = archive_read_next_header(a, &entry)) == ARCHIVE_EOF) {
			break;
		} else if (r < ARCHIVE_OK) {
			LOG_W("%s\n", archive_error_string(a));
		} else if (r < ARCHIVE_WARN) {
			LOG_E("%s\n", archive_error_string(a));
			goto ret;
		}

		path_join(NULL, &path, dest_path, archive_entry_pathname(entry));

		archive_entry_copy_pathname(entry, path.buf);

		if ((r = archive_write_header(ext, entry)) < ARCHIVE_OK) {
			LOG_W("%s\n", archive_error_string(ext));
		} else if (archive_entry_size(entry) > 0) {
			if ((r = copy_data(a, ext)) < ARCHIVE_OK) {
				LOG_W("%s\n", archive_error_string(ext));
			} else if (r < ARCHIVE_WARN) {
				LOG_E("%s\n", archive_error_string(ext));
				goto ret;
			}
		}

		if ((r = archive_write_finish_entry(ext)) < ARCHIVE_OK) {
			LOG_W("%s\n", archive_error_string(ext));
		} else if (r < ARCHIVE_WARN) {
			LOG_E("%s\n", archive_error_string(ext));
			goto ret;
		}
	}

	res = true;
ret:
	sbuf_destroy(&path);

	if (a) {
		archive_read_close(a);
		archive_read_free(a);
	}

	if (ext) {
		archive_write_close(ext);
		archive_write_free(ext);
	}
	return res;
}
