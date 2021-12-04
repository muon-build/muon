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
static const uint32_t serial_version = 3;

static bool
dump_uint32(uint32_t v, FILE *f)
{
	return fs_fwrite(&v, sizeof(uint32_t), f);
}

static bool
dump_uint64(uint64_t v, FILE *f)
{
	return fs_fwrite(&v, sizeof(uint64_t), f);
}

static bool
load_uint32(uint32_t *v, FILE *f)
{
	return fs_fread(v, sizeof(uint32_t), f);
}

static bool
load_uint64(uint64_t *v, FILE *f)
{
	return fs_fread(v, sizeof(uint64_t), f);
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

static bool
dump_big_strings(struct workspace *wk, FILE *f)
{
	uint64_t len = 0;
	uint64_t start, end;

	// dump a placeholder at the start
	if (!fs_ftell(f, &start)) {
		return false;
	} else if (!dump_uint64(0, f)) {
		return false;
	}

	uint32_t i;
	for (i = 1; i < wk->objs.len; ++i) {
		struct obj *o = bucket_array_get(&wk->objs, i);
		if (o->type != obj_string) {
			continue;
		}

		struct str *ss = &o->dat.str;
		if (!(ss->flags & str_flag_big)) {
			continue;
		}

		if (!fs_fwrite(ss->s, ss->len + 1, f)) {
			return false;
		}

		ss->serialized_offset = len;

		len += ss->len + 1;
	}

	// jump back to the beginning, write the length, and then return
	if (!fs_ftell(f, &end)) {
		return false;
	} else if (!fs_fseek(f, start)) {
		return false;
	} else if (!dump_uint64(len, f)) {
		return false;
	} else if (!fs_fseek(f, end)) {
		return false;
	}

	return true;
}

struct big_string_table {
	uint8_t *data;
	uint64_t len;
};

static bool
load_big_strings(struct workspace *wk, struct big_string_table *bst, FILE *f)
{
	uint64_t len;
	uint8_t *buf = NULL;
	if (!load_uint64(&len, f)) {
		return false;
	}

	if (len) {
		assert(len < UINT32_MAX);
		buf = z_calloc(1, len);
		if (!fs_fread(buf, len, f)) {
			return false;
		}
	}

	*bst = (struct big_string_table){
		.data = buf,
		.len = len,
	};

	return true;
}

struct serial_str {
	uint64_t s, len;
	enum str_flags flags;
};

static bool
get_big_string(struct workspace *wk, const struct big_string_table *bst, struct serial_str *src, struct str *res)
{
	assert(src->flags & str_flag_big);

	assert(src->s < bst->len && "invalid big_string");

	char *buf = z_calloc(1, src->len + 1);
	memcpy(buf, &bst->data[src->s], src->len);

	*res = (struct str) {
		.flags = src->flags,
		.len = src->len,
		.s = buf,
	};

	return true;
}

static bool
dump_objs(struct workspace *wk, FILE *f)
{
	if (!dump_uint32(wk->objs.len - 1, f)) {
		return false;
	}

	struct serial_str ser_s;
	// memsan is upset about uninitialized padding bytes in this struct
	// when we try to write it out.  A better solution would be to not
	// write the padding bytes to disk, but that would complicate the code.
	memset(&ser_s, 0, sizeof(struct serial_str));

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

			ser_s = (struct serial_str) {
				.len = ss->len,
				.flags = ss->flags,
			};

			if (ss->flags & str_flag_big) {
				ser_s.s = ss->serialized_offset;
			} else {
				if (!bucket_array_lookup_pointer(&wk->chrs, (uint8_t *)ss->s, &ser_s.s)) {
					assert(false && "pointer not found");
				}
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
load_objs(struct workspace *wk, const struct big_string_table *bst, FILE *f)
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
				if (!get_big_string(wk, bst, &ser_s, &o.dat.str)) {
					return false;
				}
			} else {
				o.dat.str = (struct str){
					.s = bucket_array_get(&wk->chrs, ser_s.s),
					.len = ser_s.len,
					.flags = ser_s.flags,
				};
			}
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
	      && dump_big_strings(&wk_dest, f)
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

	struct big_string_table bst;

	uint32_t obj_src;
	if (!(load_serial_header(f)
	      && load_uint32(&obj_src, f)
	      && load_bucket_array(&wk_src.chrs, f)
	      && load_big_strings(&wk_src, &bst, f)
	      && load_objs(&wk_src, &bst, f))) {
		goto ret;
	}

	/* obj_fprintf(&wk_src, log_file(), "loaded %o\n", obj_src); */

	if (!obj_clone(&wk_src, wk, obj_src, obj)) {
		goto ret;
	}

	ret = true;
ret:
	if (bst.data) {
		z_free(bst.data);
	}
	workspace_destroy_bare(&wk_src);
	return ret;
}
