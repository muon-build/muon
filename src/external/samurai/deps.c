/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <ctype.h>
#include <inttypes.h>
#include <string.h>

#include "external/samurai/ctx.h"
#include "lang/string.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

#include "external/samurai/build.h"
#include "external/samurai/deps.h"
#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/util.h"

/*
   .ninja_deps file format

   The header identifying the format is the string "# ninjadeps\n", followed by a
   4-byte integer specifying the format version. After this is a series of binary
   records. All integers in .ninja_deps are written in system byte-order.

   A record starts with a 4-byte integer indicating the record type and size. If
   the high bit is set, then it is a dependency record. Otherwise, it is a node
   record. In either case, the remaining 31 bits specify the size in bytes of the
   rest of the record. The size must be a multiple of 4, and no larger than than
   2^19.

   Node records are given in incrementing ID order, and must be given before any
   dependency record that refers to it. The last 4-byte integer in the record is
   used as a checksum to prevent corruption. Counting from 0, the n-th node record
   (specifying the node with ID n) will have a checksum of ~n (bitwise negation of
   n). The remaining bytes of the record specify the path of the node, padded with
   NUL bytes to the next 4-byte boundary (start of the checksum value).

   A dependency record contains a list of dependencies for the edge that built a
   particular node. The first 4-byte integer is the node ID. The second and third
   4-byte integers are the low and high 32-bits of the UNIX mtime (in nanoseconds)
   of the node when it was built. Following this is a sequence of 4-byte integers
   specifying the IDs of the dependency nodes for this edge, which will have been
   specified previously in node records.
 */

/* maximum record size (in bytes) */
#define SAMU_MAX_RECORD_SIZE (1 << 19)

static const char ninja_depsname[] = ".ninja_deps";
static const char ninja_depsheader[] = "# ninjadeps\n";
static const uint32_t ninja_depsver = 4;

static void
samu_depswrite(struct samu_ctx *ctx, const void *p, size_t n, size_t m)
{
	if (fwrite(p, n, m, ctx->deps.depsfile) != m) {
		samu_fatal("deps log write:");
	}
}

static bool
samu_recordid(struct samu_ctx *ctx, struct samu_node *n)
{
	uint32_t sz, chk;

	if (n->id != -1) {
		return false;
	}
	if (ctx->deps.entrieslen == INT32_MAX) {
		samu_fatal("too many nodes");
	}
	n->id = ctx->deps.entrieslen++;
	sz = (n->path->n + 7) & ~3;
	if (sz + 4 >= SAMU_MAX_RECORD_SIZE) {
		samu_fatal("ID record too large");
	}
	samu_depswrite(ctx, &sz, 4, 1);
	samu_depswrite(ctx, n->path->s, 1, n->path->n);
	samu_depswrite(ctx, (char[4]){ 0 }, 1, sz - n->path->n - 4);
	chk = ~n->id;
	samu_depswrite(ctx, &chk, 4, 1);

	return true;
}

static void
samu_recorddeps(struct samu_ctx *ctx, struct samu_node *out, struct samu_nodearray *deps, int64_t mtime)
{
	uint32_t sz, m;
	size_t i;

	sz = 12 + deps->len * 4;
	if (sz + 4 >= SAMU_MAX_RECORD_SIZE) {
		samu_fatal("deps record too large");
	}
	sz |= 0x80000000;
	samu_depswrite(ctx, &sz, 4, 1);
	samu_depswrite(ctx, &out->id, 4, 1);
	m = mtime & 0xffffffff;
	samu_depswrite(ctx, &m, 4, 1);
	m = (mtime >> 32) & 0xffffffff;
	samu_depswrite(ctx, &m, 4, 1);
	for (i = 0; i < deps->len; ++i) {
		samu_depswrite(ctx, &deps->node[i]->id, 4, 1);
	}
}

struct seekable_source {
	struct source src;
	uint64_t i;
};

static size_t
src_fread(void *buf, size_t sz, size_t n, struct seekable_source *src)
{
	if (!sz || !n) {
		return 0;
	}

	if (src->i >= src->src.len) {
		return 0;
	}

	size_t l = n, r = (src->src.len - src->i) / sz;
	r = r < l ? r : l;

	memcpy(buf, &src->src.src[src->i], r * sz);
	src->i += r * sz;
	return r;
}

static int
src_getc(struct seekable_source *src)
{
	if (src->i >= src->src.len) {
		return EOF;
	} else {
		char c = src->src.src[src->i];
		if (c == '\r' && src->src.src[src->i + 1] == '\n') {
			c = '\n';
			++src->i;
		}
		++src->i;
		return c;
	}
}

void
samu_depsinit(struct samu_ctx *ctx, const char *builddir)
{
	char *depspath = (char *)ninja_depsname;
	uint32_t *buf, cap, ver, sz, id;
	size_t len, i, j;
	bool isdep;
	struct samu_string *path;
	struct samu_node *n;
	struct samu_edge *e;
	struct samu_entry *entry, *oldentries;
	struct seekable_source src = { 0 };

	/* XXX: when ninja hits a bad record, it truncates the log to the last
	 * good record. perhaps we should do the same. */

	if (ctx->deps.depsfile) {
		fclose(ctx->deps.depsfile);
		ctx->deps.depsfile = NULL;
	}
	ctx->deps.entrieslen = 0;
	cap = BUFSIZ;
	buf = samu_xmalloc(ctx->a, cap);
	if (builddir) {
		samu_xasprintf(ctx->a, &depspath, "%s/%s", builddir, ninja_depsname);
	}
	if (!fs_exists(depspath)) {
		goto rewrite;
	}

	if (!fs_read_entire_file(ctx->a, depspath, &src.src)) {
		samu_warn("failed to read deps file");
		goto rewrite;
	}

	if (strncmp(src.src.src, ninja_depsheader, strlen(ninja_depsheader)) != 0) {
		samu_warn("invalid deps log header");
		goto rewrite;
	}
	src.i += strlen(ninja_depsheader);

	if (src_fread(&ver, sizeof(ver), 1, &src) != 1) {
		samu_warn("deps log truncated");
		goto rewrite;
	}
	if (ver != ninja_depsver) {
		samu_warn("unknown deps log version");
		goto rewrite;
	}
	while (true) {
		if (src_fread(&sz, sizeof(sz), 1, &src) != 1) {
			break;
		}
		isdep = sz & 0x80000000;
		sz &= 0x7fffffff;
		if (sz > SAMU_MAX_RECORD_SIZE) {
			samu_warn("deps record too large");
			goto rewrite;
		}
		if (sz > cap) {
			do {
				cap *= 2;
			} while (sz > cap);
			buf = samu_xmalloc(ctx->a, cap);
		}
		if (src_fread(buf, sz, 1, &src) != 1) {
			samu_warn("deps log truncated");
			goto rewrite;
		}
		if (sz % 4) {
			samu_warn("invalid size, must be multiple of 4: %" PRIu32, sz);
			goto rewrite;
		}
		if (isdep) {
			if (sz < 12) {
				samu_warn("invalid size, must be at least 12: %" PRIu32, sz);
				goto rewrite;
			}
			sz -= 12;
			id = buf[0];
			if (id >= ctx->deps.entrieslen) {
				samu_warn("invalid node ID: %" PRIu32, id);
				goto rewrite;
			}
			entry = &ctx->deps.entries[id];
			entry->mtime = (int64_t)buf[2] << 32 | buf[1];
			e = entry->node->gen;
			if (!e || !samu_edgevar(ctx, e, "deps", true)) {
				continue;
			}
			sz /= 4;
			entry->deps.len = sz;
			entry->deps.node = samu_xreallocarray(ctx->a, NULL, 0, sz, sizeof(n));
			for (i = 0; i < sz; ++i) {
				id = buf[3 + i];
				if (id >= ctx->deps.entrieslen) {
					samu_warn("invalid node ID: %" PRIu32, id);
					goto rewrite;
				}
				entry->deps.node[i] = ctx->deps.entries[id].node;
			}
		} else {
			if (sz <= 4) {
				samu_warn("invalid size, must be greater than 4: %" PRIu32, sz);
				goto rewrite;
			}
			if (ctx->deps.entrieslen != ~buf[sz / 4 - 1]) {
				samu_warn("corrupt deps log, bad checksum");
				goto rewrite;
			}
			if (ctx->deps.entrieslen == INT32_MAX) {
				samu_warn("too many nodes in deps log");
				goto rewrite;
			}
			len = sz - 4;
			while (((char *)buf)[len - 1] == '\0') {
				--len;
			}
			path = samu_mkstr(ctx->a, len);
			memcpy(path->s, buf, len);
			path->s[len] = '\0';

			n = samu_mknode(ctx, path);
			if (ctx->deps.entrieslen >= ctx->deps.entriescap) {
				size_t newcap = ctx->deps.entriescap ? ctx->deps.entriescap * 2 : 1024;
				ctx->deps.entries = samu_xreallocarray(ctx->a,
					ctx->deps.entries,
					ctx->deps.entriescap,
					newcap,
					sizeof(ctx->deps.entries[0]));
				ctx->deps.entriescap = newcap;
			}
			n->id = ctx->deps.entrieslen;
			ctx->deps.entries[ctx->deps.entrieslen++] = (struct samu_entry){ .node = n };
		}
	}

rewrite:
	if (ctx->deps.depsfile) {
		fclose(ctx->deps.depsfile);
		ctx->deps.depsfile = NULL;
	}
	ctx->deps.depsfile = fopen(depspath, "wb");
	if (!ctx->deps.depsfile) {
		samu_fatal("open %s:", depspath);
	}
	samu_depswrite(ctx, ninja_depsheader, 1, sizeof(ninja_depsheader) - 1);
	samu_depswrite(ctx, &ninja_depsver, 1, sizeof(ninja_depsver));

	/* reset ID for all current entries */
	for (i = 0; i < ctx->deps.entrieslen; ++i) {
		ctx->deps.entries[i].node->id = -1;
	}
	/* save a temporary copy of the old entries */
	oldentries = samu_xreallocarray(ctx->a, NULL, 0, ctx->deps.entrieslen, sizeof(ctx->deps.entries[0]));
	memcpy(oldentries, ctx->deps.entries, ctx->deps.entrieslen * sizeof(ctx->deps.entries[0]));

	len = ctx->deps.entrieslen;
	ctx->deps.entrieslen = 0;
	for (i = 0; i < len; ++i) {
		entry = &oldentries[i];
		if (!entry->deps.len) {
			continue;
		}
		samu_recordid(ctx, entry->node);
		ctx->deps.entries[entry->node->id] = *entry;
		for (j = 0; j < entry->deps.len; ++j) {
			samu_recordid(ctx, entry->deps.node[j]);
		}
		samu_recorddeps(ctx, entry->node, &entry->deps, entry->mtime);
	}
	fflush(ctx->deps.depsfile);
	if (ferror(ctx->deps.depsfile)) {
		samu_fatal("deps log write failed");
	}
}

void
samu_depsclose(struct samu_ctx *ctx)
{
	fflush(ctx->deps.depsfile);
	if (ferror(ctx->deps.depsfile)) {
		samu_fatal("deps log write failed");
	}
	fclose(ctx->deps.depsfile);
	ctx->deps.depsfile = NULL;
}

static void
samu_deps_push_node(struct samu_ctx *ctx, const struct str *f)
{
	struct samu_string *in;

	if (ctx->deps.deps.len == ctx->deps.depscap) {
		size_t newcap = ctx->deps.deps.node ? ctx->deps.depscap * 2 : 32;
		ctx->deps.deps.node = samu_xreallocarray(
			ctx->a, ctx->deps.deps.node, ctx->deps.depscap, newcap, sizeof(ctx->deps.deps.node[0]));
		ctx->deps.depscap = newcap;
	}
	in = samu_mkstr(ctx->a, f->len);
	memcpy(in->s, f->s, f->len);
	in->s[f->len] = '\0';
	ctx->deps.deps.node[ctx->deps.deps.len++] = samu_mknode(ctx, in);
}

static struct samu_nodearray *
samu_depsparse_gcc(struct samu_ctx *ctx, const char *name, bool allowmissing)
{
	struct samu_string *out = NULL;
	struct seekable_source src = { 0 };
	int c, n;
	bool sawcolon;

	ctx->deps.deps.len = 0;
	if (!fs_exists(name)) {
		if (allowmissing) {
			return &ctx->deps.deps;
		}
		return 0;
	}

	if (!fs_read_entire_file(ctx->a, name, &src.src)) {
		return 0;
	}

	sawcolon = false;
	ctx->deps.buf.len = 0;
	c = src_getc(&src);
	for (;;) {
		/* TODO: this parser needs to be rewritten to be made simpler */
		while (isalnum(c) || strchr("$+,-./@\\_()", c) || (sawcolon && c == ':')) {
			switch (c) {
			case '\\':
				/* handle the crazy escaping generated by clang and gcc */
				n = 0;
				do {
					c = src_getc(&src);
					if (++n % 2 == 0) {
						samu_bufadd(ctx->a, &ctx->deps.buf, '\\');
					}
				} while (c == '\\');
				if ((c == ' ' || c == '\t') && n % 2 != 0) {
					break;
				}
				for (; n > 2; n -= 2) {
					samu_bufadd(ctx->a, &ctx->deps.buf, '\\');
				}
				switch (c) {
				case '#': break;
				case '\n': c = ' '; continue;
				default: samu_bufadd(ctx->a, &ctx->deps.buf, '\\'); continue;
				}
				break;
			case '$':
				c = src_getc(&src);
				if (c != '$') {
					samu_warn("bad depfile[%d]: contains variable reference", (int)src.i);
					goto err;
				}
				break;
			}
			samu_bufadd(ctx->a, &ctx->deps.buf, c);
			c = src_getc(&src);
		}
		if (sawcolon) {
			if (!isspace(c) && c != EOF) {
				samu_warn("bad depfile[%d]: '%c' is not a valid target character", (int)src.i, c);
				goto err;
			}
			if (ctx->deps.buf.len > 0) {
				samu_deps_push_node(ctx,
					&(struct str){
						.s = ctx->deps.buf.data,
						.len = ctx->deps.buf.len,
					});
			}
			if (c == '\n') {
				sawcolon = false;
				do {
					c = src_getc(&src);
				} while (c == '\n');
			}
			if (c == EOF) {
				break;
			}
		} else {
			while (isblank(c)) {
				c = src_getc(&src);
			}
			if (c == EOF) {
				break;
			}
			if (c != ':') {
				samu_warn("bad depfile: expected ':', saw '%c'", c);
				goto err;
			}
			if (!out) {
				out = samu_mkstr(ctx->a, ctx->deps.buf.len);
				memcpy(out->s, ctx->deps.buf.data, ctx->deps.buf.len);
				out->s[ctx->deps.buf.len] = '\0';
			} else if (out->n != ctx->deps.buf.len
				   || memcmp(ctx->deps.buf.data, out->s, ctx->deps.buf.len) != 0) {
				samu_fatal("bad depfile: multiple outputs: %.*s != %s",
					(int)ctx->deps.buf.len,
					ctx->deps.buf.data,
					out->s);
				goto err;
			}
			sawcolon = true;
			c = src_getc(&src);
		}
		ctx->deps.buf.len = 0;
		for (;;) {
			if (c == '\\') {
				if (src_getc(&src) != '\n') {
					samu_warn("bad depfile[%d]: '\\' only allowed before newline", (int)src.i);
					printf("%s", src.src.src);
					goto err;
				}
			} else if (!isblank(c)) {
				break;
			}
			c = src_getc(&src);
		}
	}

	return &ctx->deps.deps;

err:
	if (!allowmissing) {
		samu_fatal("failed to parse depfile %s", name);
	}
	return NULL;
}

static struct samu_nodearray *
samu_depsparse_msvc(struct samu_ctx *ctx, struct tstr *out, struct samu_string *deps_prefix)
{
	const struct str prefix = deps_prefix ? STRL(deps_prefix->s) : STR("Note: including file: ");
	const char *whitespace = " \r\t";
	struct str line = { .s = out->buf, .len = 0 };
	int32_t i;
	const char *nl;
	bool seen_prefix = false;

	// Here, we are using ctx->deps.buf as the buffer for filtered output.
	ctx->deps.buf.len = 0;

	while (true) {
		if ((nl = strchr(line.s, '\n'))) {
			line.len = nl - line.s;
		} else {
			line.len = strlen(line.s);
		}

		// Trim off trailing whitespace
		for (i = line.len - 1; i >= 0; --i) {
			if (!strchr(whitespace, line.s[i])) {
				break;
			}
			--line.len;
		}

		if (!seen_prefix
			&& (str_endswithi(&line, &STR(".c")) || str_endswithi(&line, &STR(".cc"))
				|| str_endswithi(&line, &STR(".cxx")) || str_endswithi(&line, &STR(".cpp"))
				|| str_endswithi(&line, &STR(".c++")))) {
			// cl.exe likes to output the name of the compiled
			// file.  ninja filters this here so we do the same.
			goto cont;
		} else if (str_startswith(&line, &prefix)) {
			// Trim off prefix
			line.s += prefix.len;
			line.len -= prefix.len;

			// Trim off leading whitespace
			for (i = 0; (uint32_t)i < line.len; ++i) {
				if (!strchr(whitespace, line.s[i])) {
					break;
				}
			}
			line.s += i;
			line.len -= i;

			str_to_lower(&line);

			TSTR(buf);
			TSTR(path);
			tstr_pushn(ctx->wk, &buf, line.s, line.len);
			path_make_absolute(ctx->wk, &path, buf.buf);

			// Skip system headers
			if (str_contains(&line, &STR("program files"))
				|| str_contains(&line, &STR("microsoft visual studio"))) {
				goto cont;
			}

			/* L("path: '%s'", path.buf); */
			samu_deps_push_node(ctx,
				&(struct str){
					path.buf,
					path.len,
				});
		} else {
			for (i = 0; (uint32_t)i < line.len; ++i) {
				samu_bufadd(ctx->a, &ctx->deps.buf, line.s[i]);
			}

			if (nl) {
				samu_bufadd(ctx->a, &ctx->deps.buf, '\n');
			}
		}

cont:
		if (nl) {
			line.s = nl + 1;
		} else {
			break;
		}
	}

	samu_bufadd(ctx->a, &ctx->deps.buf, 0);
	return &ctx->deps.deps;
}

void
samu_depsload(struct samu_ctx *ctx, struct samu_edge *e)
{
	struct samu_string *deptype, *depfile;
	struct samu_nodearray *deps = NULL;
	struct samu_node *n;

	if (e->flags & FLAG_DEPS) {
		return;
	}
	e->flags |= FLAG_DEPS;
	n = e->out[0];
	deptype = samu_edgevar(ctx, e, "deps", true);
	if (deptype) {
		if (n->id != -1 && n->mtime <= ctx->deps.entries[n->id].mtime) {
			deps = &ctx->deps.entries[n->id].deps;
		} else if (ctx->buildopts.explain) {
			samu_warn("explain %s: missing or outdated record in .ninja_deps", n->path->s);
		}
	} else {
		depfile = samu_edgevar(ctx, e, "depfile", false);
		if (!depfile) {
			return;
		}
		deps = samu_depsparse_gcc(ctx, depfile->s, false);
		if (ctx->buildopts.explain && !deps) {
			samu_warn("explain %s: missing or invalid depfile", n->path->s);
		}
	}
	if (deps) {
		samu_edgeadddeps(ctx, e, deps->node, deps->len);
	} else {
		n->dirty = true;
		e->flags |= FLAG_DIRTY_OUT;
	}
}

void
samu_depsrecord(struct samu_ctx *ctx, struct tstr *output, const char **filtered_output, struct samu_edge *e)
{
	struct samu_string *deptype_str, *depfile;
	struct samu_nodearray *deps;
	struct samu_node *out, *n;
	struct samu_entry *entry;
	size_t i;
	bool update;
	enum {
		deptype_gcc,
		deptype_msvc,
	} deptype;

	deptype_str = samu_edgevar(ctx, e, "deps", true);
	if (!deptype_str || deptype_str->n == 0) {
		return;
	}

	if (strcmp(deptype_str->s, "msvc") == 0) {
		deptype = deptype_msvc;
	} else if (strcmp(deptype_str->s, "gcc") == 0) {
		deptype = deptype_gcc;
	} else {
		samu_warn("unsuported deps type: %s", deptype_str->s);
		return;
	}

	switch (deptype) {
	case deptype_gcc: {
		depfile = samu_edgevar(ctx, e, "depfile", false);
		if (!depfile || depfile->n == 0) {
			samu_warn("deps but no depfile");
			return;
		}
		deps = samu_depsparse_gcc(ctx, depfile->s, true);
		if (!ctx->buildopts.keepdepfile) {
			fs_remove(depfile->s);
		}
		*filtered_output = 0;
		break;
	}
	case deptype_msvc: {
		deps = samu_depsparse_msvc(ctx, output, samu_edgevar(ctx, e, "msvc_deps_prefix", true));
		*filtered_output = ctx->deps.buf.data;
		break;
	}
	}

	out = e->out[0];
	if (!deps) {
		return;
	}
	update = false;
	entry = NULL;
	if (samu_recordid(ctx, out)) {
		update = true;
	} else {
		entry = &ctx->deps.entries[out->id];
		if (entry->mtime != out->mtime || entry->deps.len != deps->len) {
			update = true;
		}
		for (i = 0; i < deps->len && !update; ++i) {
			if (entry->deps.node[i] != deps->node[i]) {
				update = true;
			}
		}
	}
	for (i = 0; i < deps->len; ++i) {
		n = deps->node[i];
		if (samu_recordid(ctx, n)) {
			update = true;
		}
	}
	if (update) {
		samu_recorddeps(ctx, out, deps, out->mtime);
		if (fflush(ctx->deps.depsfile) < 0) {
			samu_fatal("deps log flush:");
		}
	}
}
