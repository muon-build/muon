#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "lang/serial.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"

#define SERIAL_MAGIC_LEN 8
static const char serial_magic[SERIAL_MAGIC_LEN] = "muondump";
static const uint32_t serial_version = 1;

static bool
dump_uint32(uint32_t v, FILE *f)
{
	return fs_fwrite(&v, sizeof(uint32_t), f);
}

static bool
load_uint32(uint32_t *v, FILE *f)
{
	return fs_fread(v, sizeof(uint32_t), f);
}

static bool
dump_darr(const struct darr *da, FILE *f)
{
	return dump_uint32(da->len, f)
	       && fs_fwrite(da->e, da->item_size * da->len, f);
}

static bool
load_darr(struct darr *da, FILE *f)
{
	uint32_t start = da->len, len;
	if (!load_uint32(&len, f)) {
		return false;
	}

	darr_grow_by(da, len);
	assert(da->cap - (start * da->item_size) >= len * da->item_size);
	return fs_fread(da->e + start * da->item_size, len * da->item_size, f);
}

static bool
dump_bucket_array(const struct bucket_array *ba, FILE *f)
{
	uint32_t i;
	void *e;

	if (!dump_uint32(ba->len, f)) {
		return false;
	}

	for (i = 0; i < ba->len; ++i) {
		e = bucket_array_get(ba, i);
		if (!fs_fwrite(e, ba->item_size, f)) {
			return false;
		}
	}

	return true;
}

static bool
load_bucket_array(struct bucket_array *ba, FILE *f)
{
	uint32_t len;
	if (!load_uint32(&len, f)) {
		return false;
	}

	char buf[BUF_SIZE_4k];
	assert(BUF_SIZE_4k > ba->item_size && "item size too big");

	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (!fs_fread(buf, ba->item_size, f)) {
			return false;
		}

		bucket_array_push(ba, buf);
	}

	return true;
}

static bool
dump_serial_header(FILE *f)
{
	return fs_fwrite(serial_magic, SERIAL_MAGIC_LEN, f)
	       && dump_uint32(serial_version, f);
}

static bool
load_serial_header(FILE *f)
{
	char buf[SERIAL_MAGIC_LEN] = { 0 };

	if (!fs_fread(buf, SERIAL_MAGIC_LEN, f)) {
		return false;
	}

	if (memcmp(buf, serial_magic, SERIAL_MAGIC_LEN) != 0) {
		LOG_E("invalid file (missing magic)");
		return false;
	}

	uint32_t v;
	if (!load_uint32(&v, f)) {
		return false;
	}

	if (v != serial_version) {
		LOG_E("version mismatch: %d != %d", v, serial_version);
		return false;
	}

	return true;
}

bool
serial_dump(struct workspace *wk_src, uint32_t obj, FILE *f)
{
	bool ret = false;
	struct workspace wk_dest = { 0 };
	workspace_init_bare(&wk_dest);

	uint32_t obj_dest;
	if (!obj_clone(wk_src, &wk_dest, obj, &obj_dest)) {
		goto ret;
	}

	/* obj_printf(&wk_dest, "saving %o\n", obj_dest); */

	if (!(dump_serial_header(f)
	      && dump_uint32(obj_dest, f)
	      && dump_darr(&wk_dest.strs, f)
	      && dump_bucket_array(&wk_dest.objs, f))) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy_bare(&wk_dest);
	return ret;
}

bool
serial_load(struct workspace *wk, uint32_t *obj, FILE *f)
{
	bool ret = false;
	struct workspace wk_src = { 0 };
	workspace_init_bare(&wk_src);
	darr_clear(&wk_src.strs);

	uint32_t obj_src;
	if (!(load_serial_header(f)
	      && load_uint32(&obj_src, f)
	      && load_darr(&wk_src.strs, f)
	      && load_bucket_array(&wk_src.objs, f))) {
		goto ret;
	}

	/* obj_printf(&wk_src, "loaded %o\n", obj_src); */

	if (!obj_clone(&wk_src, wk, obj_src, obj)) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy_bare(&wk_src);
	return ret;
}
