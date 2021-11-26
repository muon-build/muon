#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "lang/serial.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"

#define SERIAL_MAGIC_LEN 8
static const char serial_magic[SERIAL_MAGIC_LEN] = "muondump";
static const uint32_t serial_version = 2;

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
dump_bucket_array(const struct bucket_array *ba, FILE *f)
{
	uint32_t i;

	if (!dump_uint32(ba->buckets.len, f)) {
		return false;
	}

	for (i = 0; i < ba->buckets.len; ++i) {
		struct bucket *b = darr_get(&ba->buckets, i);

		if (!dump_uint32(b->len, f)) {
			return false;
		}

		if (!fs_fwrite(b->mem, ba->item_size * b->len, f)) {
			return false;
		}
	}

	return true;
}

static bool
load_bucket_array(struct bucket_array *ba, FILE *f)
{
	uint32_t buckets_len;
	uint32_t i;
	struct bucket b = { 0 };

	if (!load_uint32(&buckets_len, f)) {
		return false;
	}

	z_free(((struct bucket *)darr_get(&ba->buckets, 0))->mem);
	darr_clear(&ba->buckets);

	for (i = 0; i < buckets_len; ++i) {
		init_bucket(ba, &b);

		if (!load_uint32(&b.len, f)) {
			return false;
		}

		if (!fs_fread(b.mem, ba->item_size * b.len, f)) {
			return false;
		}

		darr_push(&ba->buckets, &b);
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

struct serial_str {
	uint32_t s, len;
	enum str_flags flags;
};

static bool
dump_objs(struct workspace *wk, FILE *f)
{
	if (!dump_uint32(wk->objs.len - 1, f)) {
		return false;
	}

	struct serial_str ser_s;
	void *data;
	size_t len;
	uint8_t type_tag;

	assert(obj_type_count < UINT8_MAX && "increase size of type tag");

	uint32_t i;
	for (i = 1; i < wk->objs.len; ++i) {
		struct obj *o = bucket_array_get(&wk->objs, i);
		type_tag = o->type;

		if (!fs_fwrite(&type_tag, sizeof(uint8_t), f)) {
			return false;
		}

		if (o->type == obj_string) {
			const struct str *ss = &o->dat.str;

			if (ss->flags & str_flag_big) {
				assert(false && "TODO: serialize big strings");
			}

			ser_s = (struct serial_str) {
				.len = ss->len,
				.flags = ss->flags,
			};

			if (!bucket_array_lookup_pointer(&wk->chrs, (uint8_t *)ss->s, &ser_s.s)) {
				assert(false && "pointer not found");
			}

			data = &ser_s;
			len = sizeof(struct serial_str);
		} else {
			data = &o->dat;
			len = sizeof(o->dat);
		}

		if (!fs_fwrite(data, len, f)) {
			return false;
		}
	}

	return true;
}

static bool
load_objs(struct workspace *wk, FILE *f)
{
	uint32_t len;
	if (!load_uint32(&len, f)) {
		return false;
	}

	if (!len) {
		return true;
	}

	uint8_t type_tag;
	struct serial_str ser_s;
	struct obj o;

	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (!fs_fread(&type_tag, sizeof(uint8_t), f)) {
			return false;
		}

		o = (struct obj) { .type = type_tag, };

		if (type_tag == obj_string) {
			if (!fs_fread(&ser_s, sizeof(struct serial_str), f)) {
				return false;
			}

			if (ser_s.flags & str_flag_big) {
				assert(false && "TODO: serialize big strings");
			}

			o.dat.str = (struct str){
				.s = bucket_array_get(&wk->chrs, ser_s.s),
				.len = ser_s.len,
				.flags = ser_s.flags,
			};
		} else {
			if (!fs_fread(&o.dat, sizeof(o.dat), f)) {
				return false;
			}
		}

		bucket_array_push(&wk->objs, &o);
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

	/* obj_fprintf(&wk_dest, log_file(), "saving %o\n", obj_dest); */

	if (!(dump_serial_header(f)
	      && dump_uint32(obj_dest, f)
	      && dump_bucket_array(&wk_dest.chrs, f)
	      && dump_objs(&wk_dest, f))) {
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

	uint32_t obj_src;
	if (!(load_serial_header(f)
	      && load_uint32(&obj_src, f)
	      && load_bucket_array(&wk_src.chrs, f)
	      && load_objs(&wk_src, f))) {
		goto ret;
	}

	/* obj_fprintf(&wk_src, log_file(), "loaded %o\n", obj_src); */

	if (!obj_clone(&wk_src, wk, obj_src, obj)) {
		goto ret;
	}

	ret = true;
ret:
	workspace_destroy_bare(&wk_src);
	return ret;
}
