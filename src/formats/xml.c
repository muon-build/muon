/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "formats/xml.h"
#include "lang/object_iterators.h"
#include "lang/workspace.h"

struct xml_node {
	obj name;
	obj attr;
	obj children;
};

/*******************************************************************************
 * manipulation
 ******************************************************************************/

obj
xml_node_new(struct xml_writer *w, const char *name)
{
	obj n = name ? make_str(w->wk, name) : 0;
	obj idx = w->nodes.len;
	bucket_arr_push(&w->nodes, &(struct xml_node){ .name = n });
	return idx;
}

void
xml_node_push_attr(struct xml_writer *w, obj idx, const char *key, obj v)
{
	struct xml_node *attr, *node = bucket_arr_get(&w->nodes, idx);
	if (!node->attr) {
		make_obj(w->wk, &node->attr, obj_array);
	}

	obj a = xml_node_new(w, key);
	attr = bucket_arr_get(&w->nodes, a);
	attr->children = make_strf(w->wk, "\"%s\"", get_cstr(w->wk, v));

	obj_array_push(w->wk, node->attr, a);
}

void
xml_node_push_child(struct xml_writer *w, obj idx, obj child)
{
	struct xml_node *node = bucket_arr_get(&w->nodes, idx);
	if (!node->children) {
		make_obj(w->wk, &node->children, obj_array);
	}

	obj_array_push(w->wk, node->children, child);
}

/*******************************************************************************
 * init/destroy
 ******************************************************************************/

void
xml_writer_init(struct workspace *wk, struct xml_writer *w)
{
	*w = (struct xml_writer){
		.wk = wk,
	};

	bucket_arr_init(&w->nodes, sizeof(struct xml_node), 1024);
}

void
xml_writer_destroy(struct xml_writer *w)
{
	bucket_arr_destroy(&w->nodes);
}

/*******************************************************************************
 * writing
 ******************************************************************************/

static void
xml_indent(struct xml_writer *w, FILE *out)
{
	uint32_t i;
	for (i = 0; i < w->indent; ++i) {
		fputc('\t', out);
	}
}

static void
xml_write_node(struct xml_writer *w, struct xml_node *node, FILE *out)
{
	obj idx;
	struct xml_node *attr;

	if (node->name) {
		xml_indent(w, out);
		fprintf(out, "<%s", get_cstr(w->wk, node->name));

		if (node->attr) {
			++w->indent;
			obj_array_for(w->wk, node->attr, idx) {
				attr = bucket_arr_get(&w->nodes, idx);
				fprintf(out, "\n");
				xml_indent(w, out);
				fprintf(out, "%s=%s", get_cstr(w->wk, attr->name), get_cstr(w->wk, attr->children));
			}
			--w->indent;
		}

		fprintf(out, ">\n");
	}

	if (node->children) {
		++w->indent;
		obj_array_for(w->wk, node->children, idx) {
			xml_write_node(w, bucket_arr_get(&w->nodes, idx), out);
		}
		--w->indent;
	}

	if (node->name) {
		xml_indent(w, out);
		fprintf(out, "</%s>\n", get_cstr(w->wk, node->name));
	}
}

void
xml_write(struct xml_writer *w, obj root, FILE *out)
{
	fprintf(out, "%s", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	xml_write_node(w, bucket_arr_get(&w->nodes, root), out);
}
