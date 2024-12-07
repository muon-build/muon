/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <ImGuiColorTextEdit/TextEditor.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <math.h>

#include "ui/IconsFontAwesome5.h"

extern "C" {
#include "backend/common_args.h"
#include "lang/object_iterators.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"
#include "ui/inspector.h"
#include "ui/ui.h"
}

static uint32_t
fnv_1a(uint8_t *v, uint32_t len)
{
	const uint32_t prime = 16777619;
	const uint32_t offset_basis = 2166136261;
	uint32_t hash = offset_basis;
	for (uint32_t i = 0; i < len; ++i) {
		hash ^= v[i];
		hash *= prime;
	}
	return hash;
}

static float
clamp(float a, float min, float max)
{
	if (a < min) {
		return min;
	} else if (a > max) {
		return max;
	} else {
		return a;
	}
}

struct inspector_node {
	ImVec2 pos, size, force;
	uint32_t inputs_len, outputs_len;
	obj id;

	inspector_node(obj id, uint32_t inputs_count, uint32_t outputs_count)
	{
		this->id = id;
		this->pos = { (float)drand48() - 0.5f, (float)drand48() - 0.5f };
		this->pos *= 250.0;
		this->pos += { 100, 100 };
		this->inputs_len = inputs_count;
		this->outputs_len = outputs_count;
	}

	ImVec2
	input_slot_pos(int slot_no, float zoom) const
	{
		return ImVec2(pos.x * zoom, pos.y * zoom + size.y * ((float)slot_no + 1) / ((float)inputs_len + 1));
	}

	ImVec2
	output_slot_pos(int slot_no, float zoom) const
	{
		return ImVec2(
			pos.x * zoom + size.x, pos.y * zoom + size.y * ((float)slot_no + 1) / ((float)outputs_len + 1));
	}

	const char *
	name(struct workspace *wk) const
	{
		switch (get_obj_type(wk, id)) {
		case obj_alias_target:
		case obj_custom_target:
		case obj_both_libs:
		case obj_build_target: {
			return get_cstr(wk, ca_backend_tgt_name(wk, id));
		}
		case obj_file: {
			const char *path = get_file_path(wk, id);
			if (path_is_subpath(wk->source_root, path)) {
				return path + strlen(wk->source_root) + 1;
			} else {
				return path;
			}
		}
		case obj_string: {
			const char *path = get_cstr(wk, id);
			if (path_is_subpath(wk->source_root, path)) {
				return path + strlen(wk->source_root) + 1;
			} else {
				return path;
			}
		}
		case obj_dependency: {
			return get_cstr(wk, get_obj_dependency(wk, id)->name);
		}
		default: return obj_type_to_s(get_obj_type(wk, id));
		}
	}

	const char *
	icon(struct workspace *wk) const
	{
		switch (get_obj_type(wk, id)) {
		case obj_alias_target: return ICON_FA_BIOHAZARD;
		case obj_custom_target: return ICON_FA_BOX;
		case obj_both_libs: return ICON_FA_BOOK;
		case obj_build_target: {
			enum tgt_type type = get_obj_build_target(wk, id)->type;

			if ((type & tgt_static_library) || (type & tgt_dynamic_library)) {
				return ICON_FA_BOOK;
			} else if (type & tgt_shared_module) {
				return ICON_FA_BOOK_OPEN;
			} else if (type & tgt_executable) {
				return ICON_FA_BOLT;
			}
			break;
		}
		case obj_file: return ICON_FA_FILE;
		default: break;
		}

		return ICON_FA_QUESTION;
	}
};

struct inspector_node_link {
	uint32_t input_idx, input_slot, output_idx, output_slot;

	inspector_node_link(int input_idx, int input_slot, int output_idx, int output_slot)
	{
		this->input_idx = input_idx;
		this->input_slot = input_slot;
		this->output_idx = output_idx;
		this->output_slot = output_slot;
	}
};

struct inspector_window {
	const char *name;
	void (*render)(inspector_window *);
	bool open;
};

struct editor_window {
	char file[1024] = {};
	TextEditor editor;
	bool open;

	editor_window(const char *file)
		: editor(TextEditor())
		, open(true)
	{
		uint32_t len = strlen(file) + 1;
		if (len > sizeof(this->file)) {
			len = sizeof(this->file) - 1;
		}
		memcpy(this->file, file, len);
		this->file[len] = 0;
	}

	friend class TextEditor;
};

struct breakpoint {
	char file[1024];
	uint32_t col, line;
};

struct inspector_context {
	// Initialization
	bool init = false, reinit = false;

	// Workspace
	struct workspace wk;
	struct sbuf log;

	// Windows
	ImVector<inspector_window> windows;
	ImVector<editor_window> editor_windows;
	ImGuiID dock_id_right;

	// Breakpoints and debugging
	bool stopped_at_breakpoint = false;
	ImVector<breakpoint> breakpoints;
	ImVector<std::string> expressions;
	obj callstack;

	// Graph params
	ImVector<inspector_node> nodes;
	ImVector<inspector_node_link> links;
	struct {
		bool show_grid = false;
		float c1, c2, c3, c4, c5;
		float zoom, zoom_tgt;
		ImVec2 scroll, scroll_tgt;
		uint32_t node_selected = 0;
	} graph_params;
};

static int32_t
id_to_node_idx(struct inspector_context *ctx, obj id)
{
	for (int32_t i = 0; i < ctx->nodes.size(); ++i) {
		if (ctx->nodes[i].id == id) {
			return i;
		}
	}

	return -1;
}

static bool
node_is_linked_to(struct inspector_context *ctx, uint32_t a, uint32_t b)
{
	for (inspector_node_link &link : ctx->links) {
		if ((link.input_idx == a && link.output_idx == b) || (link.input_idx == b && link.output_idx == a)) {
			return true;
		}
	}

	return false;
}

static void
relax_nodes(struct inspector_context *ctx)
{
	for (int32_t i = 0; i < ctx->nodes.size(); ++i) {
		struct inspector_node &a = ctx->nodes[i];
		float dist;

		for (int32_t j = i + 1; j < ctx->nodes.size(); ++j) {
			struct inspector_node &b = ctx->nodes[j];

			bool linked = node_is_linked_to(ctx, i, j);
			ImVec2 diff = a.pos - b.pos;
			diff /= ctx->graph_params.c5;
			dist = sqrt(diff.x * diff.x + diff.y * diff.y);

			if (dist < 5.0f) {
				linked = false;
			}

			diff /= dist;

			float mag;
			if (linked) {
				mag = ctx->graph_params.c1 * logf(dist / ctx->graph_params.c2);
			} else {
				mag = -ctx->graph_params.c3 / (dist * dist);
			}

			if (fabs(mag) < 0.001f) {
				continue;
			} else if (fabs(mag) > 10.0f) {
				continue;
			}

			diff *= mag;
			a.force -= diff;
			b.force += diff;
		}
	}

	for (int32_t i = 0; i < ctx->nodes.size(); ++i) {
		struct inspector_node &a = ctx->nodes[i];

		/* ImVec2 center = { 100, 100 }; */
		/* ImVec2 diff = a.pos - center; */
		/* diff /= 100.0f; */
		/* float dist = sqrt(diff.x * diff.x + diff.y * diff.y); */
		/* if (dist > 1.0f) { */
		/* 	diff /= dist; */
		/* 	float mag = -1 / (dist * dist); */
		/* 	diff *= mag; */
		/* 	a.force += diff; */
		/* } */

		a.pos += a.force * ctx->graph_params.c4;
		a.force = { 0, 0 };
	}
}

static void
add_dependency_link(struct inspector_context *ctx, int32_t dest, obj d)
{
	int32_t d_id = id_to_node_idx(ctx, d);
	if (d_id == -1) {
		d_id = ctx->nodes.size();
		ctx->nodes.push_back(inspector_node(d, 0, 0));
	}

	inspector_node &dep = ctx->nodes[d_id];
	++dep.outputs_len;

	inspector_node &node = ctx->nodes[dest];
	++node.inputs_len;

	ctx->links.push_back(inspector_node_link(dest, node.inputs_len - 1, d_id, dep.outputs_len - 1));
}

static void
add_recursive_deps(struct inspector_context *ctx, int32_t dest, obj d)
{
	struct workspace *wk = &ctx->wk;
	struct obj_dependency *t = get_obj_dependency(wk, d);

	if (t->dep.raw.deps) {
		obj_array_for(wk, t->dep.raw.deps, d) {
			add_recursive_deps(ctx, dest, d);
		}
	}
	if (t->dep.raw.link_with) {
		obj_array_for(wk, t->dep.raw.link_with, d) {
			add_dependency_link(ctx, dest, d);
		}
	}
	if (t->dep.raw.link_whole) {
		obj_array_for(wk, t->dep.raw.link_whole, d) {
			add_dependency_link(ctx, dest, d);
		}
	}
}

struct inspector_context *
get_inspector_context()
{
	static struct inspector_context _ctx;
	struct inspector_context *ctx = &_ctx;
	return ctx;
}

static void
sync_breakpoints()
{
	struct inspector_context *ctx = get_inspector_context();

	for (breakpoint &bp : ctx->breakpoints) {
		for (editor_window &win : ctx->editor_windows) {
			if (strcmp(win.file, bp.file) == 0) {
				win.editor.mBreakpoints.insert(bp.line);
			}
		}
	}
}

static void
push_breakpoint(const char *file, uint32_t line, uint32_t col)
{
	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;

	breakpoint bp = { .line = line, .col = col };
	cstr_copy(bp.file, file, sizeof(bp.file));
	ctx->breakpoints.push_back(bp);
	vm_dbg_push_breakpoint(wk, make_str(wk, file), line);
	sync_breakpoints();
}

static void
render_editor(editor_window &window)
{
	if (!ImGui::Begin(window.file, &window.open)) {
		ImGui::End();
		return;
	}

	extern ImFont *gMonospaceFont;
	ImGui::PushFont(gMonospaceFont);
	window.editor.Render(window.file);
	ImGui::PopFont();

	if (ImGui::BeginPopupContextItem("editor context menu")) {
		if (ImGui::Selectable("Add breakpoint")) {
			TextEditor::Coordinates coords = window.editor.GetCursorPosition();
			push_breakpoint(window.file, coords.mLine + 1, coords.mColumn + 1);
		}

		ImGui::EndPopup();
	}

	ImGui::End();
}

static void
safe_path_relative_to(struct workspace *wk, struct sbuf *sbuf, const char *base, const char *path)
{
	if (path_is_absolute(path)) {
		path_relative_to(wk, sbuf, base, path);
	} else {
		path_copy(wk, sbuf, path);
	}
}

static void
open_editor(struct source *src, uint32_t line, uint32_t col)
{
	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;

	SBUF(rel);
	safe_path_relative_to(wk, &rel, wk->source_root, src->label);

	TextEditor::Coordinates coords(line - 1, col - 1);

	for (editor_window &win : ctx->editor_windows) {
		if (strcmp(win.file, rel.buf) == 0) {
			win.open = true;
			win.editor.SetCursorPosition(coords);
			ImGui::SetWindowFocus(win.file);
			return;
		}
	}

	ctx->editor_windows.reserve(ctx->editor_windows.size() + 1);
	++ctx->editor_windows.Size;
	editor_window &win = ctx->editor_windows[ctx->editor_windows.size() - 1];
	memset(&win, 0, sizeof(editor_window));
	win = editor_window(rel.buf);
	win.editor.SetPalette(TextEditor::GetDarkPalette());
	win.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Meson());
	win.editor.SetReadOnly(true);
	win.editor.SetText(src->src);
	win.editor.SetCursorPosition(coords);
	win.editor.SetShowWhitespaces(false);

	/* wk->vm.dbg_state.breakpoints; */
	/* win.editor.SetBreakpoints(); */

	ImGui::DockBuilderDockWindow(win.file, ctx->dock_id_right);

	sync_breakpoints();

	/* std::unordered_set<int> lineNumbers; */
	/* for (auto line : annotatedSource.linesWithTags) */
	/*     lineNumbers.insert(line.lineNumber + 1); */
	/* editor.SetBreakpoints(lineNumbers); */
}

struct source *
source_lookup_by_name(struct workspace *wk, const char *path)
{
	struct source *src = 0;

	for (uint32_t i = 0; i < wk->vm.src.len; ++i) {
		src = (struct source *)arr_get(&wk->vm.src, i);
		if (strcmp(src->label, path) == 0) {
			break;
		}
	}

	return src;
}

struct source *
obj_callstack_unpack(obj e, uint32_t *line, uint32_t *col)
{
	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;

	obj path_, line_, col_;
	obj_array_index(wk, e, 0, &path_);
	obj_array_index(wk, e, 1, &line_);
	obj_array_index(wk, e, 2, &col_);

	const char *path = get_cstr(wk, path_);
	*line = get_obj_number(wk, line_);
	*col = get_obj_number(wk, col_);

	return source_lookup_by_name(wk, path);
}

static void
open_editor_for_object(obj t)
{
	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;

	if (get_obj_type(wk, t) != obj_build_target) {
		return;
	}

	ctx->callstack = get_obj_build_target(wk, t)->callstack;
	obj e;
	obj_array_index(wk, ctx->callstack, 0, &e);

	uint32_t line, col;
	struct source *src = obj_callstack_unpack(e, &line, &col);
	open_editor(src, line, col);
}

static void
render_callstack(inspector_window *window)
{
	if (!ImGui::Begin(window->name, &window->open)) {
		ImGui::End();
		return;
	}

	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;

	if (ctx->callstack) {
		obj e;
		obj_array_for(wk, ctx->callstack, e) {
			uint32_t line, col;
			struct source *src = obj_callstack_unpack(e, &line, &col);

			SBUF(rel);
			char label[1024];
			safe_path_relative_to(wk, &rel, wk->source_root, src->label);
			snprintf(label, sizeof(label), "%s:%d:%d", rel.buf, line, col);

			if (ImGui::Selectable(label)) {
				open_editor(src, line, col);
			}
		}
	}

	ImGui::End();
}

static void
render_breakpoints(inspector_window *window)
{
	if (!ImGui::Begin(window->name, &window->open)) {
		ImGui::End();
		return;
	}

	struct inspector_context *ctx = get_inspector_context();

	for (breakpoint &bp : ctx->breakpoints) {
		char label[1024];
		snprintf(label, sizeof(label), "%s:%d:%d", bp.file, bp.line, bp.col);
		if (ImGui::Selectable(label)) {
		}
	}

	ImGui::End();
}

static void
render_expressions(inspector_window *window)
{
	if (!ImGui::Begin(window->name, &window->open)) {
		ImGui::End();
		return;
	}

	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;

	struct vm_dbg_state empty_state = {};
	stack_push(&wk->stack, wk->vm.dbg_state, empty_state);

	for (int32_t i = 0; i < ctx->expressions.Size; ++i) {
		ImGui::PushID(i);

		std::string& s = ctx->expressions[i];

		if (ImGui::SmallButton(ICON_FA_MINUS)) {
			ctx->expressions.erase(&s);
		}

		ImGui::SameLine();


		obj res = 0;
		SBUF(res_str);
		if (eval_str(wk, s.c_str(), eval_mode_repl, &res)) {
			obj_to_s(wk, res, &res_str);
		} else {
			sbuf_pushs(wk, &res_str, "<error>");
		}


		ImGui::LabelText(s.c_str(), "%s", res_str.buf);

		ImGui::PopID();
	}

	stack_pop(&wk->stack, wk->vm.dbg_state);

	static char buf[1024];
	if (ImGui::SmallButton(ICON_FA_PLUS) && buf[0]) {
		ctx->expressions.push_back(buf);
		buf[0] = 0;
	}
	ImGui::SameLine();
	ImGui::InputText("new", buf, sizeof(buf));

	ImGui::End();
}

static void
render_log(inspector_window *window)
{
	if (!ImGui::Begin(window->name, &window->open)) {
		ImGui::End();
		return;
	}

	struct inspector_context *ctx = get_inspector_context();
	/* struct workspace *wk = &ctx->wk; */

	ImGui::TextUnformatted(ctx->log.buf, ctx->log.buf + ctx->log.len);

	ImGui::End();
}

static void
render_node_graph(inspector_window *window)
{
	if (!ImGui::Begin("Graph", &window->open)) {
		ImGui::End();
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;
	ImVector<inspector_node> &nodes = ctx->nodes;
	ImVector<inspector_node_link> &links = ctx->links;

	relax_nodes(ctx);

	uint32_t node_hovered_in_list = 0;
	uint32_t node_hovered_in_scene = 0;

	ImGui::SameLine();
	ImGui::BeginGroup();

	const float NODE_SLOT_RADIUS = 4.0f;
	const ImVec2 NODE_WINDOW_PADDING(8.0f, 8.0f);

	// Create our child canvas
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("c1", &ctx->graph_params.c1, 0, 20, "%f", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("c2", &ctx->graph_params.c2, 0, 100, "%f", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("c3", &ctx->graph_params.c3, 0, 20, "%f", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("c4", &ctx->graph_params.c4, 0, 20, "%f", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("c5", &ctx->graph_params.c5, 0, 500, "%f", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::Checkbox("Show grid", &ctx->graph_params.show_grid);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(60, 60, 70, 200));
	ImGui::BeginChild("scrolling_region",
		ImVec2(0, 0),
		true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PushItemWidth(120.0f);

	if (io.WantCaptureMouse && ImGui::IsWindowHovered()) {
		ctx->graph_params.zoom_tgt += io.MouseWheel * 0.1f;
		ctx->graph_params.zoom_tgt = clamp(ctx->graph_params.zoom_tgt, 0.1f, 5.0f);
	}

	float new_zoom = ctx->graph_params.zoom + (ctx->graph_params.zoom_tgt - ctx->graph_params.zoom) * 0.1;

	/* 0,0---------
	 * |    |   |
	 * |    |   |          real    scaled off    scaled
	 * |----x   |  - z = 1 (1, 1), (1, 1) (0, 0) (0, 0)
	 * |        |
	 * |--------x  - z = 2 (1, 1), (2, 2) (1, 1) (.5, .5)
	 * |
	 */
	ImDrawList *draw_list = ImGui::GetWindowDrawList();

	if (io.WantCaptureMouse && ImGui::IsWindowHovered()) {
		const ImVec2 offset_px = (ImGui::GetWindowSize() * 0.5) * ctx->graph_params.zoom;
		/* ImVec2 mouse_px = ctx->graph_params.scroll; // - ImGui::GetCursorScreenPos() + ImGui::GetMousePos(); */
		ImVec2 mouse_pre = (ctx->graph_params.scroll) * ctx->graph_params.zoom + offset_px;
		ImVec2 mouse_post = (ctx->graph_params.scroll) * new_zoom + offset_px;

		/* draw_list->AddCircleFilled(mouse_px, 12, IM_COL32(255, 0, 0, 255)); */
		/* draw_list->AddCircleFilled(mouse_post, 10, IM_COL32(0, 0, 255, 155)); */
		/* draw_list->AddCircleFilled(mouse_pre, 8, IM_COL32(0, 255, 0, 155)); */
		/* draw_list->AddCircleFilled(ImGui::GetMousePos(), 6, IM_COL32(244, 255, 0, 155)); */

		ImVec2 mouse_diff_px = (mouse_pre - mouse_post) / new_zoom;
		/* L("%f | %f, %f | %f, %f | %f, %f", new_zoom, ctx->graph_params.scroll.x, ctx->graph_params.scroll.y, ImGui::GetMousePos().x, ImGui::GetMousePos().y, ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y); */

		ctx->graph_params.scroll_tgt += mouse_diff_px;
	}

	ctx->graph_params.scroll
		= ctx->graph_params.scroll_tgt; //(ctx->graph_params.scroll_tgt - ctx->graph_params.scroll) * 0.1;

	ctx->graph_params.zoom = new_zoom;

	ImGui::SetWindowFontScale(0.5 + ctx->graph_params.zoom * 0.5);

	const ImVec2 offset = (ImGui::GetCursorScreenPos() + ctx->graph_params.scroll) * ctx->graph_params.zoom;

	// Display grid
	if (ctx->graph_params.show_grid) {
		ImU32 GRID_COLOR = IM_COL32(200, 200, 200, 40);
		float GRID_SZ = 64.0f * ctx->graph_params.zoom;
		ImVec2 win_pos = ImGui::GetCursorScreenPos() * ctx->graph_params.zoom;
		ImVec2 canvas_sz = ImGui::GetWindowSize();
		for (float x = fmodf(ctx->graph_params.scroll.x * ctx->graph_params.zoom, GRID_SZ); x < canvas_sz.x;
			x += GRID_SZ) {
			draw_list->AddLine(ImVec2(x, 0.0f) + win_pos, ImVec2(x, canvas_sz.y) + win_pos, GRID_COLOR);
		}
		for (float y = fmodf(ctx->graph_params.scroll.y * ctx->graph_params.zoom, GRID_SZ); y < canvas_sz.y;
			y += GRID_SZ) {
			draw_list->AddLine(ImVec2(0.0f, y) + win_pos, ImVec2(canvas_sz.x, y) + win_pos, GRID_COLOR);
		}
	}

	// Display links
	draw_list->ChannelsSplit(2);
	draw_list->ChannelsSetCurrent(0); // Background
	for (int link_idx = 0; link_idx < links.Size; link_idx++) {
		inspector_node_link *link = &links[link_idx];
		inspector_node *node_inp = &nodes[link->input_idx];
		inspector_node *node_out = &nodes[link->output_idx];
		ImVec2 p1 = offset + node_inp->input_slot_pos(link->input_slot, ctx->graph_params.zoom);
		ImVec2 p2 = offset + node_out->output_slot_pos(link->output_slot, ctx->graph_params.zoom);

		uint32_t clr = fnv_1a((uint8_t *)&node_inp->id, 4) | IM_COL32_A_MASK;
		if (ctx->graph_params.node_selected && node_out->id != ctx->graph_params.node_selected) {
			clr = IM_COL32(200, 200, 200, 100);
		}

		draw_list->AddBezierCubic(p1, p1 + ImVec2(+50, 0), p2 + ImVec2(-50, 0), p2, clr, 3.0f);
	}

	// Display nodes
	for (int node_idx = 0; node_idx < nodes.Size; node_idx++) {
		inspector_node *node = &nodes[node_idx];
		ImGui::PushID(node->id);
		ImVec2 node_rect_min = offset + node->pos * ctx->graph_params.zoom;

		// Display node contents first
		draw_list->ChannelsSetCurrent(1); // Foreground
		bool old_any_active = ImGui::IsAnyItemActive();
		ImGui::SetCursorScreenPos(node_rect_min + NODE_WINDOW_PADDING);
		ImGui::BeginGroup(); // Lock horizontal position
		ImGui::Text("%s%s", node->icon(wk), node->name(wk));
		ImGui::EndGroup();

		// Save the size of what we have emitted and whether any of the widgets are being used
		bool node_widgets_active = (!old_any_active && ImGui::IsAnyItemActive());
		node->size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
		ImVec2 node_rect_max = node_rect_min + node->size;

		// Display node box
		draw_list->ChannelsSetCurrent(0); // Background
		ImGui::SetCursorScreenPos(node_rect_min);
		ImGui::InvisibleButton("node", node->size);
		if (ImGui::IsItemHovered()) {
			node_hovered_in_scene = node->id;
		}
		bool node_moving_active = ImGui::IsItemActive();
		if (node_widgets_active || node_moving_active) {
			ctx->graph_params.node_selected = node->id;
		}
		if (node_moving_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			node->pos = node->pos + io.MouseDelta / ctx->graph_params.zoom;
		}

		ImU32 node_bg_color
			= (node_hovered_in_list == node->id || node_hovered_in_scene == node->id
				  || (node_hovered_in_list == 0 && ctx->graph_params.node_selected == node->id)) ?
				  IM_COL32(75, 75, 75, 255) :
				  IM_COL32(60, 60, 60, 255);
		draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 4.0f);
		draw_list->AddRect(node_rect_min, node_rect_max, IM_COL32(100, 100, 100, 255), 4.0f);
		/* ImVec2 pos = offset + node->input_slot_pos(0, ctx->graph_params.zoom); */
		/* L("%f, %f", pos.x, pos.y); */
		for (uint32_t slot_idx = 0; slot_idx < node->inputs_len; slot_idx++)
			draw_list->AddCircleFilled(offset + node->input_slot_pos(slot_idx, ctx->graph_params.zoom),
				NODE_SLOT_RADIUS,
				IM_COL32(150, 150, 150, 150));
		for (uint32_t slot_idx = 0; slot_idx < node->outputs_len; slot_idx++)
			draw_list->AddCircleFilled(offset + node->output_slot_pos(slot_idx, ctx->graph_params.zoom),
				NODE_SLOT_RADIUS,
				IM_COL32(150, 150, 150, 150));

		ImGui::PopID();
	}

	draw_list->ChannelsMerge();

	// Clear selection
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) || !ImGui::IsAnyItemHovered()) {
			ctx->graph_params.node_selected = node_hovered_in_list = node_hovered_in_scene = 0;
		}
	}

	// Scrolling
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()
		&& ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
		ctx->graph_params.scroll_tgt = ctx->graph_params.scroll_tgt + io.MouseDelta / ctx->graph_params.zoom;
	}

	ImGui::PopItemWidth();
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::EndGroup();

	ImGui::End();
}

static void
render_sidebar(inspector_window *window)
{
	if (!ImGui::Begin("Targets", &window->open)) {
		ImGui::End();
		return;
	}

	struct inspector_context *ctx = get_inspector_context();
	struct workspace *wk = &ctx->wk;
	struct project *proj;

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = (struct project *)arr_get(&wk->projects, i);
		if (!proj->cfg.name) {
			continue;
		}

		if (ImGui::TreeNodeEx(get_cstr(wk, proj->cfg.name), ImGuiTreeNodeFlags_None)) {
			obj t;
			obj_array_for(wk, proj->targets, t) {
				if (ImGui::Selectable(get_cstr(wk, ca_backend_tgt_name(wk, t)))) {
					open_editor_for_object(t);
				}
			}
			ImGui::TreePop();
		}
	}

	ImGui::End();
}

static void
inspector_break_cb(struct workspace *wk, struct source *src, uint32_t line, uint32_t col)
{
	struct inspector_context *ctx = get_inspector_context();

	wk->vm.dbg_state.icount = 0;

	if (line) {
		ctx->callstack = vm_callstack(wk);

		open_editor(src, line, col);
		ctx->stopped_at_breakpoint = true;
		while (ctx->stopped_at_breakpoint) {
			ui_update();
		}
	} else {
		ui_update();
	}
}

static void
reinit_inspector_context(struct inspector_context *ctx, bool first = false)
{
	struct workspace *wk = &ctx->wk;

	ctx->reinit = false;

	if (ctx->init && !first) {
		workspace_destroy(wk);
	}

	sbuf_clear(&ctx->log);

	ctx->nodes.clear();
	ctx->links.clear();

	ctx->callstack = 0;

	workspace_init_bare(wk);
	workspace_init_runtime(wk);

	wk->vm.dbg_state.break_after = 1024;
	for (breakpoint &bp : ctx->breakpoints) {
		vm_dbg_push_breakpoint(wk, make_str(wk, bp.file), bp.line);
	}
	wk->vm.dbg_state.break_cb = inspector_break_cb;

	workspace_do_setup(wk, "build-tmp", "muon", 0, 0);

	// Clear callstack again after setup
	ctx->callstack = 0;

	obj t;
	uint32_t i;
	struct project *proj;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = (struct project *)arr_get(&wk->projects, i);
		obj_array_for(wk, proj->targets, t) {
			ctx->nodes.push_back(inspector_node(t, 0, 0));
		}
	}

	for (int32_t i = 0; i < ctx->nodes.size(); ++i) {
		inspector_node node = ctx->nodes[i];
		obj id = node.id, d;
		switch (get_obj_type(wk, node.id)) {
		case obj_alias_target: {
			struct obj_alias_target *t = get_obj_alias_target(wk, id);
			obj_array_for(wk, t->depends, d) {
				add_dependency_link(ctx, i, d);
			}
			break;
		}
		case obj_custom_target: {
			struct obj_custom_target *t = get_obj_custom_target(wk, id);
			if (t->input) {
				obj_array_for(wk, t->input, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			if (t->depends) {
				obj_array_for(wk, t->depends, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			break;
		}
		case obj_both_libs:
			id = get_obj_both_libs(wk, id)->dynamic_lib;
			//fallthrough
		case obj_build_target: {
			struct obj_build_target *t = get_obj_build_target(wk, id);
			if (t->dep_internal.raw.deps) {
				obj_array_for(wk, t->dep_internal.raw.deps, d) {
					add_recursive_deps(ctx, i, d);
				}
			}
			if (t->dep_internal.raw.link_with) {
				obj_array_for(wk, t->dep_internal.raw.link_with, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			if (t->dep_internal.raw.link_whole) {
				obj_array_for(wk, t->dep_internal.raw.link_whole, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			break;
		}
		case obj_dependency: {
			struct obj_dependency *t = get_obj_dependency(wk, id);
			if (t->dep.raw.deps) {
				obj_array_for(wk, t->dep.raw.deps, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			if (t->dep.raw.link_with) {
				obj_array_for(wk, t->dep.raw.link_with, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			if (t->dep.raw.link_whole) {
				obj_array_for(wk, t->dep.raw.link_whole, d) {
					add_dependency_link(ctx, i, d);
				}
			}
			break;
		}
		default: break;
		}
	}
}

void
ui_inspector_window()
{
	struct inspector_context *ctx = get_inspector_context();

	static bool imgui_debug = false;
	if (imgui_debug) {
		ImGui::ShowDebugLogWindow();
		ImGui::ShowDemoWindow();
	}

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Rerun")) {
				ctx->reinit = true;
			}
			if (ImGui::MenuItem("Show ImGui Debug Log")) {
				imgui_debug = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window")) {
			for (inspector_window &win : ctx->windows) {
				if (ImGui::MenuItem(win.name, win.open ? ICON_FA_CHECK : "")) {
					win.open = !win.open;
				}
			}
			ImGui::EndMenu();
		}

		if (ctx->stopped_at_breakpoint) {
			if (ImGui::SmallButton(ICON_FA_ARROW_ALT_CIRCLE_RIGHT)) {
				ctx->wk.vm.dbg_state.stepping = true;
				ctx->stopped_at_breakpoint = false;
			}

			if (ImGui::SmallButton(ICON_FA_PLAY)) {
				ctx->wk.vm.dbg_state.stepping = false;
				ctx->stopped_at_breakpoint = false;
			}
		}

		ImGui::EndMainMenuBar();
	}

	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace", nullptr, window_flags);
	ImGui::PopStyleVar();
	ImGui::PopStyleVar(2);

	// DockSpace
	ImGuiID dockspace_id = ImGui::GetID("RootDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

	static bool first_time = true;
	if (first_time) {
		first_time = false;

		ImGui::DockBuilderRemoveNode(dockspace_id); // clear any previous layout
		ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

		ImGuiID dock_id_right, dock_id_left;
		ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &dock_id_left, &dock_id_right);

		ImGuiID dock_id_left_up, dock_id_left_down;
		ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Up, 0.8f, &dock_id_left_up, &dock_id_left_down);

		ctx->dock_id_right = dock_id_right;

		// we now dock our windows into the docking node we made above
		ImGui::DockBuilderDockWindow("Graph", dock_id_right);
		ImGui::DockBuilderDockWindow("Log", dock_id_right);
		ImGui::DockBuilderDockWindow("Targets", dock_id_left_up);
		ImGui::DockBuilderDockWindow("Callstack", dock_id_left_down);
		ImGui::DockBuilderDockWindow("Breakpoints", dock_id_left_down);
		ImGui::DockBuilderDockWindow("Expressions", dock_id_left_down);
	}

	ImGui::End();

	for (inspector_window &win : ctx->windows) {
		if (win.open) {
			win.render(&win);
		}
	}

	for (editor_window &win : ctx->editor_windows) {
		if (win.open) {
			render_editor(win);
		}
	}

	ImGui::DockBuilderFinish(dockspace_id);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	(void)scancode;
	(void)window;

	if (action != GLFW_RELEASE) {
		return;
	}

	struct inspector_context *ctx = get_inspector_context();

	if (key == GLFW_KEY_W  && (mods & GLFW_MOD_CONTROL)) {
		ImGuiContext& g = *GImGui;
		ImGuiWindow *cur_window = g.NavWindow;

		if (!cur_window) {
			return;
		}

		for (inspector_window &win : ctx->windows) {
			if (win.open && strcmp(cur_window->Name, win.name) == 0) {
				win.open = false;
			}
		}

		for (editor_window &win : ctx->editor_windows) {
			if (win.open && strcmp(cur_window->Name, win.file) == 0) {
				win.open = false;
			}
		}
	}

	if (ctx->stopped_at_breakpoint) {
		if (key == GLFW_KEY_S  && (mods & GLFW_MOD_CONTROL)) {
			ctx->wk.vm.dbg_state.stepping = true;
			ctx->stopped_at_breakpoint = false;
		} else if (key == GLFW_KEY_C  && (mods & GLFW_MOD_CONTROL)) {
			ctx->wk.vm.dbg_state.stepping = false;
			ctx->stopped_at_breakpoint = false;
		}
	}
}

void
ui_update()
{
	inspector_context *ctx = get_inspector_context();

	static bool first = true;
	if (first) {
		first = false;

		sbuf_init(&ctx->log, 0, 0, sbuf_flag_overflow_alloc);
		log_set_buffer(&ctx->log);

		ctx->graph_params.c1 = 0.5;
		ctx->graph_params.c2 = 9.0;
		ctx->graph_params.c3 = 3.0;
		ctx->graph_params.c4 = 5.0;
		ctx->graph_params.c5 = 40.0;
		ctx->graph_params.zoom_tgt = 1.0f;
		ctx->graph_params.scroll_tgt = { 0, 0 };

		ctx->windows.push_back({ "Targets", render_sidebar, true });
		ctx->windows.push_back({ "Breakpoints", render_breakpoints, true });
		ctx->windows.push_back({ "Expressions", render_expressions, true });
		ctx->windows.push_back({ "Callstack", render_callstack, true });
		ctx->windows.push_back({ "Log", render_log, true });
		ctx->windows.push_back({ "Graph", render_node_graph, true });

		ctx->init = true;
		reinit_inspector_context(ctx, true);
	}

	if (ctx->reinit) {
		reinit_inspector_context(ctx);
	}

	extern struct g_win g_win;
	const ImVec4 clear_color = ImVec4(40.0 / 256.0, 42 / 256.0, 54 / 256.0, 1.00f);

	ImGuiIO &io = ImGui::GetIO();

	// Poll and handle events (inputs, window resize, etc.)
	glfwPollEvents();
	if (glfwGetWindowAttrib(g_win.window, GLFW_ICONIFIED) != 0) {
		ImGui_ImplGlfw_Sleep(10);
		return;
	}

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ui_inspector_window();

	// Rendering
	ImGui::Render();
	int display_w, display_h;
	glfwGetFramebufferSize(g_win.window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(clear_color.x * clear_color.w,
		clear_color.y * clear_color.w,
		clear_color.z * clear_color.w,
		clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// Update and Render additional Platform Windows
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		GLFWwindow *backup_current_context = glfwGetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent(backup_current_context);
	}

	glfwSwapBuffers(g_win.window);
}

