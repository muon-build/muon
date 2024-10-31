/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "backend/backend.h"
#include "backend/vs.h"
#include "backend/vs/xml.h"
#include "error.h"

/* https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-filters-files?view=msvc-170 */

enum vs_filter_guid {
	VS_FILTER_GUID_HEADER,
	VS_FILTER_GUID_RESOURCE,
	VS_FILTER_GUID_SOURCE,
	VS_FILTER_GUID_LAST,
};

/* same order than enum vs_filter_guid above ! */
/* https://github.com/JamesW75/visual-studio-project-type-guid */
static const char *vs_filter_guid[VS_FILTER_GUID_LAST] = {
	"93995380-89BD-4b04-88EB-625FBE52EBFB",
	"67DA6AB6-F800-4c08-8B7A-83BB121AAD01",
	"4FC737F1-C7A5-4376-A066-2A32D752A2FF"
};

bool
vs_write_filter(struct workspace *wk, void *_ctx, FILE *out)
{
	struct vs_ctx *ctx = _ctx;
	struct xml_node nul = { out, 0, -1 };
	struct arr attributes;
	arr_init(&attributes, 1, sizeof(struct xml_attribute));

	fprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");

	/* root */
	ATTR_PUSH("DefaultTargets", "Build");
	ATTR_PUSH("ToolsVersion", ctx->vs_version == 16 ? "16.0" : "17.0");
	ATTR_PUSH("xmlns", "http://schemas.microsoft.com/developer/msbuild/2003");

	struct xml_node root = tag(wk, nul, "Project", &attributes, false);

	struct xml_node n1 = tag(wk, root, "ItemGroup", NULL, false);

	/* filter - header files */
	arr_clear(&attributes);
	ATTR_PUSH("Include", "Project");
	struct xml_node n2 = tag(wk, n1, "Filter", &attributes, false);
	SBUF_manual(guid);
	sbuf_pushf(wk, &guid, "{%s}", vs_filter_guid[VS_FILTER_GUID_HEADER]);
	tag_elt(n2, "UniqueIdentifier", guid.buf);
	tag_elt(n2, "Extensions", "h;hh;hpp;hxx;hm;inl;inc;ipp;xsd");
	tag_end(wk, n2);

	/* filter - resource files */
	arr_clear(&attributes);
	sbuf_clear(&guid);
	ATTR_PUSH("Include", "Resource Files");
	n2 = tag(wk, n1, "Filter", &attributes, false);
	sbuf_pushf(wk, &guid, "{%s}", vs_filter_guid[VS_FILTER_GUID_RESOURCE]);
	tag_elt(n2, "UniqueIdentifier", guid.buf);
	tag_elt(n2, "Extensions", "rc;ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe;resx;tiff;tif;png;wav;mfcribbon-ms");
	tag_end(wk, n2);

	/* filter - source files */
	arr_clear(&attributes);
	sbuf_clear(&guid);
	ATTR_PUSH("Include", "Source Files");
	n2 = tag(wk, n1, "Filter", &attributes, false);
	sbuf_pushf(wk, &guid, "{%s}", vs_filter_guid[VS_FILTER_GUID_SOURCE]);
	tag_elt(n2, "UniqueIdentifier", guid.buf);
	tag_elt(n2, "Extensions", "cpp;c;cc;cxx;def;odl;idl;hpj;bat;asm;asmx");
	tag_end(wk, n2);

	sbuf_destroy(&guid);

	tag_end(wk, n1);

	tag_end(wk, root);

	return true;
}
