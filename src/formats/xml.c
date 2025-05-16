/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdarg.h>

#include "formats/xml.h"
#include "lang/object_iterators.h"
#include "lang/workspace.h"

struct xml_node {
	obj name;
	obj attr;
	obj children;
	obj elt;
	enum xml_writer_style style;
};

/*******************************************************************************
 * manipulation
 ******************************************************************************/

obj
xml_node_new_styled(struct xml_writer *w, const char *name, enum xml_writer_style style, ...)
{
	obj n = name ? make_str(w->wk, name) : 0;
	obj idx = w->nodes.len;
	obj elt = 0;
	if (style & xml_writer_style_single_line_element) {
		va_list p;
		va_start(p, style);
		char *str = va_arg(p, char *);
		elt = str ? make_str(w->wk, str) : 0;
		va_end(p);
	}
	bucket_arr_push(&w->nodes, &(struct xml_node){ .name = n, .style = style, .elt = elt });
	return idx;
}

obj
xml_node_new(struct xml_writer *w, const char *name)
{
	return xml_node_new_styled(w, name, 0);
}

void
xml_node_push_attr(struct xml_writer *w, obj idx, const char *key, obj v)
{
	struct xml_node *attr, *node = bucket_arr_get(&w->nodes, idx);
	if (!node->attr) {
		node->attr = make_obj(w->wk, obj_array);
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
		node->children = make_obj(w->wk, obj_array);
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
xml_write_indent(struct xml_writer *w, FILE *out)
{
	uint32_t i;
	for (i = 0; i < w->indent; ++i) {
		fputc('\t', out);
	}
}

static void
xml_indent(struct xml_writer *w, bool ml)
{
	if (ml) {
		++w->indent;
	}
}

static void
xml_dedent(struct xml_writer *w, bool ml)
{
	if (ml) {
		--w->indent;
	}
}

static void
xml_sep(struct xml_writer *w, FILE *out, bool ml)
{
	fprintf(out, ml ? "\n" : " ");
	if (ml) {
		xml_write_indent(w, out);
	}
}

static void
xml_write_node(struct xml_writer *w, struct xml_node *node, FILE *out)
{
	obj idx;
	struct xml_node *attr;
	enum xml_writer_style style = node->style ? node->style : w->_style;

	if (node->name && style & xml_writer_style_single_line_element) {
		fprintf(out, "<%s>%s</%s>",
			get_cstr(w->wk, node->name),
			get_cstr(w->wk, node->elt),
			get_cstr(w->wk, node->name));
		return;
	}

	const bool attr_ml = !(style & xml_writer_style_single_line_attributes);

	if (node->name) {
		fprintf(out, "<%s", get_cstr(w->wk, node->name));

		if (style & xml_writer_style_space_around_attributes) {
			fprintf(out, " ");
		}

		if (node->attr) {
			xml_indent(w, attr_ml);

			obj_array_for(w->wk, node->attr, idx) {
				attr = bucket_arr_get(&w->nodes, idx);
				xml_sep(w, out, attr_ml);
				fprintf(out, "%s=%s", get_cstr(w->wk, attr->name), get_cstr(w->wk, attr->children));
			}

			xml_dedent(w, attr_ml);
		}

		if (style & xml_writer_style_space_around_attributes) {
			fprintf(out, " ");
		}

		if (attr_ml) {
			fprintf(out, ">");
		}
	}

	if (node->children) {
		xml_indent(w, attr_ml);
		obj_array_for(w->wk, node->children, idx) {
			xml_sep(w, out, attr_ml);
			xml_write_node(w, bucket_arr_get(&w->nodes, idx), out);
		}
		xml_dedent(w, attr_ml);
	}

	if (node->name) {
		xml_sep(w, out, attr_ml);
		if (attr_ml) {
			fprintf(out, "</%s>", get_cstr(w->wk, node->name));
		} else {
			fprintf(out, "/>");
		}
	}
}

void
xml_write(struct xml_writer *w, obj root, FILE *out)
{
	fprintf(out, "%s", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	xml_write_node(w, bucket_arr_get(&w->nodes, root), out);

	fprintf(out, "\n");
}
