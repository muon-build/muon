/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "backend/vs/xml.h"

struct xml_node
tag(struct workspace *wk, struct xml_node parent, const char *tag, struct arr *attributes, bool end)
{
	struct xml_node n;

	n.out = parent.out;
	n.tag = make_str(wk, tag);
	n.indent = parent.indent + 1;
	n.end = end;
	for (uint32_t i = 0; i < n.indent; i++) {
		fprintf(n.out, "\t");
	}

	fprintf(n.out, "<%s", tag);
	if (attributes) {
		for (uint32_t i = 0; i < attributes->len; i++) {
			fprintf(n.out, " %s=\"%s\"",
				((struct xml_attribute *)arr_get(attributes, i))->name,
				((struct xml_attribute *)arr_get(attributes, i))->value);
		}
	}
	n.end ? fprintf(n.out, " />\n") : fprintf(n.out, ">\n");;

	return n;
}

void tag_end(struct workspace *wk, struct xml_node n)
{
	if (n.end) {
		return;
	}

	for (uint32_t i = 0; i < n.indent; i++) {
		fprintf(n.out, "\t");
	}
	fprintf(n.out, "</%s>\n", get_cstr(wk, n.tag));
}

void tag_elt(struct xml_node parent, const char *tag, const char *elt)
{
	for (uint32_t i = 0; i < parent.indent + 1; i++) {
		fprintf(parent.out, "\t");
	}
	fprintf(parent.out, "<%s>%s</%s>\n", tag, elt, tag);
}
