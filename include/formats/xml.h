/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_XML_H
#define MUON_FORMATS_XML_H

#include <stdio.h>

#include "lang/types.h"
#include "datastructures/bucket_arr.h"

enum xml_writer_style {
	xml_writer_style_space_around_attributes = 1 << 0,
	xml_writer_style_single_line_attributes = 1 << 1,
	xml_writer_style_single_line_element = 1 << 1,
};

struct xml_writer {
	struct workspace *wk;
	struct bucket_arr nodes;
	enum xml_writer_style _style;
	uint32_t indent;
};

void xml_writer_init(struct workspace *wk, struct xml_writer *w);
void xml_writer_destroy(struct xml_writer *w);

obj xml_node_new(struct xml_writer *w, const char *name);
obj xml_node_new_styled(struct xml_writer *w, const char *name, enum xml_writer_style style);
void xml_node_push_attr(struct xml_writer *w, obj idx, const char *key, obj v);
void xml_node_push_child(struct xml_writer *w, obj idx, obj child);

void xml_write(struct xml_writer *w, obj root, FILE *out);

#endif
