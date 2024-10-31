/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_VS_XML_H
#define MUON_BACKEND_VS_XML_H

#include "compat.h"

#include "datastructures/arr.h"
#include "lang/workspace.h"

#define ATTR_PUSH(a, b) \
do { \
	struct xml_attribute attr = { a, b }; \
	arr_push(&attributes, &attr); \
} while (0)

struct xml_attribute
{
	const char *name;
	const char *value;
};

struct xml_node
{
	FILE *out;
	obj tag;
	uint32_t indent;
	bool end;
};

struct xml_node tag(struct workspace *wk, struct xml_node parent, const char *tag, struct arr *attributes, bool end);

void tag_end(struct workspace *wk, struct xml_node n);

void tag_elt(struct xml_node parent, const char *tag, const char *elt);

#endif
