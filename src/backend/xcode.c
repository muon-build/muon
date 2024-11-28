/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>

#include "backend/common_args.h"
#include "backend/output.h"
#include "backend/xcode.h"
#include "datastructures/bucket_arr.h"
#include "error.h"
#include "formats/xml.h"
#include "lang/object_iterators.h"
#include "platform/filesystem.h"
#include "platform/path.h"

struct xc_ctx {
	struct workspace *wk;
	struct stack stack;
	struct xml_writer xw;
	FILE *out;
	struct project *proj;
	struct bucket_arr pbx_item;
	uint32_t indent;
	obj pbx_objects;
	bool master_project;
	const char *proj_path, *pbx_path;
	obj legacy_target_uuid;
	obj tgt_build_files;
};

static void
xc_write_indent(struct xc_ctx *ctx)
{
	uint32_t i;
	for (i = 0; i < ctx->indent; ++i) {
		fputc('\t', ctx->out);
	}
}

static void
xc_write_comment(struct xc_ctx *ctx, obj comment)
{
	fprintf(ctx->out, "/* %s */", get_cstr(ctx->wk, comment));
}

static obj
xc_obj_uuid(struct xc_ctx *ctx, obj o, uint32_t variant)
{
	return make_strf(ctx->wk, "000000000000%02x00%08x", variant, o);
}

static obj
xc_bool(struct xc_ctx *ctx, bool b)
{
	return make_str(ctx->wk, b ? "YES" : "NO");
}

static obj
xc_quoted_str(struct xc_ctx *ctx, const char *s)
{
	return make_strf(ctx->wk, "\"%s\"", s);
}

static obj
xc_quoted(struct xc_ctx *ctx, obj s)
{
	return xc_quoted_str(ctx, get_cstr(ctx->wk, s));
}

/*
 *
 * target
 * |------ BuildFile ---- FileRef
 * |------ BuildFile ---- FileRef
 * |------ BuildFile ---- FileRef
 */

/*******************************************************************************
 * pbx_item
 ******************************************************************************/

struct pbx_item {
	obj key;
	obj value;
	obj comment;
};

static enum obj_type
xc_pbx_type(struct xc_ctx *ctx, obj pbx)
{
	obj o;
	obj_array_index(ctx->wk, pbx, 0, &o);
	return (enum obj_type)o;
}

static obj
xc_pbx_push(struct xc_ctx *ctx, obj pbx, obj key, obj value, obj comment)
{
	obj idx = ctx->pbx_item.len;
	bucket_arr_push(&ctx->pbx_item,
		&(struct pbx_item){
			.key = key,
			.value = value,
			.comment = comment,
		});

	if (key || value) {
		assert(xc_pbx_type(ctx, pbx) == (key ? obj_dict : obj_array));
	}

	obj_array_push(ctx->wk, pbx, idx);

	return key;
}

static void
xc_pbx_push_v(struct xc_ctx *ctx, obj pbx, obj value)
{
	xc_pbx_push(ctx, pbx, 0, value, 0);
}

static obj
xc_pbx_push_kv(struct xc_ctx *ctx, obj pbx, const char *key, obj value)
{
	return xc_pbx_push(ctx, pbx, make_str(ctx->wk, key), value, 0);
}

static obj
xc_pbx_push_root_object(struct xc_ctx *ctx, obj pbx)
{
	return xc_pbx_push(ctx, ctx->pbx_objects, xc_obj_uuid(ctx, pbx, 0), pbx, 0);
}

static obj
xc_pbx_new(struct xc_ctx *ctx, enum obj_type t)
{
	obj pbx;
	make_obj(ctx->wk, &pbx, obj_array);
	obj_array_push(ctx->wk, pbx, (obj)t);
	return pbx;
}

static obj
xc_pbx_new_t(struct xc_ctx *ctx, const char *type)
{
	obj pbx = xc_pbx_new(ctx, obj_dict);
	xc_pbx_push_kv(ctx, pbx, "isa", make_str(ctx->wk, type));
	return pbx;
}

static obj
xc_pbx_new_group(struct xc_ctx *ctx, obj *children)
{
	obj pbx = xc_pbx_new_t(ctx, "PBXGroup");
	*children = xc_pbx_new(ctx, obj_array);
	xc_pbx_push_kv(ctx, pbx, "children", *children);
	return pbx;
}

static void
xc_pbx_write(struct xc_ctx *ctx, obj pbx)
{
	struct workspace *wk = ctx->wk;
	obj idx;
	struct pbx_item *item;
	bool first = true, empty = get_obj_array(wk, pbx)->len == 1;
	enum { pbx_dict, pbx_array } style = xc_pbx_type(ctx, pbx) == obj_dict ? pbx_dict : pbx_array;
	const char *chars[] = {
		[pbx_dict] = "{};",
		[pbx_array] = "(),",
	};
	enum { c_opener, c_closer, c_sep };

	fprintf(ctx->out, "%c%s", chars[style][c_opener], empty ? "" : "\n");
	++ctx->indent;

	obj_array_for(wk, pbx, idx) {
		// skip the first element of the array
		if (first) {
			first = false;
			continue;
		}

		item = bucket_arr_get(&ctx->pbx_item, idx);

		xc_write_indent(ctx);

		if (item->comment) {
			xc_write_comment(ctx, item->comment);
			fprintf(ctx->out, " ");
		}

		if (item->key) {
			fprintf(ctx->out, "%s ", get_cstr(wk, item->key));

			/* if (get_obj_type(wk, item->value) == obj_array) { */
			/* 	xc_write_comment(ctx, item->comment); */
			/* } */

			fprintf(ctx->out, "= ");
		}

		if (item->value) {
			switch (get_obj_type(wk, item->value)) {
			case obj_array: {
				xc_pbx_write(ctx, item->value);
				break;
			}
			case obj_number: {
				fprintf(ctx->out, "%" PRId64, get_obj_number(wk, item->value));
				break;
			}
			case obj_string: {
				fprintf(ctx->out, "%s", get_cstr(wk, item->value));
				break;
			}
			default: UNREACHABLE;
			}

			fprintf(ctx->out, "%c", chars[style][c_sep]);
		}

		fprintf(ctx->out, "\n");
	}

	--ctx->indent;
	if (!empty) {
		xc_write_indent(ctx);
	}
	fprintf(ctx->out, "%c", chars[style][c_closer]);
}

/*******************************************************************************
 * entry
 ******************************************************************************/

static obj
xc_file(struct xc_ctx *ctx, const char *name, const char *path)
{
	obj pbx = xc_pbx_new_t(ctx, "PBXFileReference");

	obj file_type;
	enum compiler_language lang;
	if (filename_to_compiler_language(path, &lang)) {
		file_type = make_strf(
			ctx->wk, "sourcecode.%s.%s", compiler_language_to_s(lang), compiler_language_to_s(lang));
	} else {
		file_type = make_str(ctx->wk, "text");
	}

	xc_pbx_push_kv(ctx, pbx, "explicitFileType", file_type);
	xc_pbx_push_kv(ctx, pbx, "fileEncoding", make_number(ctx->wk, 4));
	xc_pbx_push_kv(ctx, pbx, "name", xc_quoted_str(ctx, name));
	xc_pbx_push_kv(ctx, pbx, "path", xc_quoted_str(ctx, path));
	xc_pbx_push_kv(ctx, pbx, "sourceTree", make_str(ctx->wk, "SOURCE_ROOT"));

	return xc_pbx_push(ctx, ctx->pbx_objects, xc_obj_uuid(ctx, pbx, 0), pbx, 0);
}

static obj
xc_build_file(struct xc_ctx *ctx, obj file_uuid)
{
	obj pbx = xc_pbx_new_t(ctx, "PBXBuildFile");

	xc_pbx_push_kv(ctx, pbx, "fileRef", file_uuid);

	return xc_pbx_push(ctx, ctx->pbx_objects, xc_obj_uuid(ctx, pbx, 0), pbx, 0);
}

static int32_t
xc_source_sort(struct workspace *wk, void *_ctx, obj a, obj b)
{
	return strcmp(get_file_path(wk, a), get_file_path(wk, b));
}

static obj
xc_project_target_sources(struct xc_ctx *ctx, struct project *proj, obj tgt, obj name, obj sources)
{
	obj pbx, pbx_children;
	pbx = xc_pbx_new_group(ctx, &pbx_children);
	xc_pbx_push_kv(ctx, pbx, "sourceTree", xc_quoted_str(ctx, "<group>"));
	xc_pbx_push_kv(ctx, pbx, "name", xc_quoted(ctx, name));

	obj pbx_build_files = 0;
	obj pbx_build = 0;
	if (tgt) {
		pbx_build_files = xc_pbx_new(ctx, obj_array);
		pbx_build = xc_pbx_new_t(ctx, "PBXSourcesBuildPhase");
		xc_pbx_push_kv(ctx, pbx_build, "buildActionMask", make_number(ctx->wk, 0x7fffffff));
		xc_pbx_push_kv(ctx, pbx_build, "runOnlyForDeploymentPostprocessing", make_number(ctx->wk, 0));
		xc_pbx_push_kv(ctx, pbx_build, "files", pbx_build_files);
	}

	if (!sources) {
		goto done;
	}

	obj sorted;
	obj_array_sort(ctx->wk, ctx, sources, xc_source_sort, &sorted);

	SBUF(rel);
	SBUF(prev_dirname);
	SBUF(dirname);
	SBUF(basename);
	const char *base = get_cstr(ctx->wk, proj->source_root);

	uint32_t stack_base = ctx->stack.len;

	obj f;
	obj_array_for(ctx->wk, sorted, f) {
		const char *path = get_file_path(ctx->wk, f);
		if (!path_is_subpath(base, path)) {
			xc_pbx_push_v(ctx, pbx_children, xc_file(ctx, path, path));
			continue;
		}

		path_relative_to(ctx->wk, &rel, base, path);
		path_copy(ctx->wk, &prev_dirname, dirname.buf);
		path_dirname(ctx->wk, &dirname, rel.buf);

		if (strcmp(dirname.buf, ".") == 0) {
			dirname.buf[0] = 0;
			dirname.len = 0;
		}

		if (strcmp(prev_dirname.buf, dirname.buf) != 0) {
			uint32_t i;
			uint32_t common_len = 0, len = 0;

			// common len
			for (i = 0; i < prev_dirname.len && i < dirname.len; ++i) {
				if (prev_dirname.buf[i] != dirname.buf[i]) {
					break;
				}

				if (prev_dirname.buf[i] == PATH_SEP) {
					++common_len;
				}
			}

			if (i && (!prev_dirname.buf[i] || !dirname.buf[i])) {
				++common_len;
			}

			for (i = 0, len = 0; i < prev_dirname.len; ++i) {
				if (prev_dirname.buf[i] == PATH_SEP || i == prev_dirname.len - 1) {
					++len;

					if (len > common_len) {
						stack_pop(&ctx->stack, pbx_children);
					}
				}
			}

			const char *start = dirname.buf;
			for (i = 0, len = 0; i < dirname.len; ++i) {
				if (dirname.buf[i] == PATH_SEP || i == dirname.len - 1) {
					++len;

					if (len > common_len) {
						uint32_t part_len = (dirname.buf + i) - start;
						if (i == dirname.len - 1) {
							++part_len;
						}
						obj path_part = make_strn(ctx->wk, start, part_len);

						obj new_pbx, new_pbx_children;
						new_pbx = xc_pbx_new_group(ctx, &new_pbx_children);
						xc_pbx_push_kv(
							ctx, new_pbx, "sourceTree", xc_quoted_str(ctx, "<group>"));
						xc_pbx_push_kv(ctx, new_pbx, "name", xc_quoted(ctx, path_part));
						xc_pbx_push_v(ctx, pbx_children, xc_pbx_push_root_object(ctx, new_pbx));
						stack_push(&ctx->stack, pbx_children, new_pbx_children);
					}

					start = dirname.buf + i + 1;
				}
			}
		}

		path_basename(ctx->wk, &basename, rel.buf);

		obj file_uuid = xc_file(ctx, basename.buf, path);
		xc_pbx_push_v(ctx, pbx_children, file_uuid);
		if (tgt) {
			xc_pbx_push_v(ctx, pbx_build_files, xc_build_file(ctx, file_uuid));
		}
	}

	ctx->stack.len = stack_base;

done:
	if (tgt) {
		obj_dict_seti(ctx->wk, ctx->tgt_build_files, tgt, xc_pbx_push_root_object(ctx, pbx_build));
	}
	return xc_pbx_push_root_object(ctx, pbx);
}

static obj
xc_project_target_custom_tgt(struct xc_ctx *ctx, struct project *proj, obj tgt)
{
	struct obj_custom_target *t = get_obj_custom_target(ctx->wk, tgt);

	return xc_project_target_sources(ctx, proj, tgt, ca_backend_tgt_name(ctx->wk, tgt), t->input);
}

static obj
xc_project_target_build_tgt(struct xc_ctx *ctx, struct project *proj, obj tgt)
{
	struct obj_build_target *t = get_obj_build_target(ctx->wk, tgt);

	obj files;
	obj_array_dup(ctx->wk, t->src, &files);
	if (t->extra_files) {
		obj_array_extend(ctx->wk, files, t->extra_files);
	}

	return xc_project_target_sources(ctx, proj, tgt, ca_backend_tgt_name(ctx->wk, tgt), files);
}

static obj
xc_project_target_group(struct xc_ctx *ctx, struct project *proj, obj tgt)
{
	obj pbx, pbx_children;
	pbx = xc_pbx_new_group(ctx, &pbx_children);
	xc_pbx_push_kv(ctx, pbx, "sourceTree", xc_quoted_str(ctx, "<group>"));
	xc_pbx_push_kv(ctx, pbx, "name", xc_quoted(ctx, ca_backend_tgt_name(ctx->wk, tgt)));

	obj child = 0;

	switch (get_obj_type(ctx->wk, tgt)) {
	case obj_alias_target: {
		// Not sure how to handle these?
		break;
	}
	case obj_both_libs: tgt = get_obj_both_libs(ctx->wk, tgt)->dynamic_lib;
	/* fallthrough */
	case obj_build_target: return xc_project_target_build_tgt(ctx, proj, tgt); break;
	case obj_custom_target: return xc_project_target_custom_tgt(ctx, proj, tgt); break;
	default: UNREACHABLE;
	}

	if (child) {
		xc_pbx_push_v(ctx, pbx_children, child);
	}

	return xc_pbx_push_root_object(ctx, pbx);
}

static obj
xc_project_meson_group(struct xc_ctx *ctx, struct project *proj)
{
	obj files, f;
	make_obj(ctx->wk, &files, obj_array);

	const char *path, *proj_source_root = get_cstr(ctx->wk, proj->source_root);

	obj_array_for(ctx->wk, ctx->wk->regenerate_deps, f) {
		path = get_cstr(ctx->wk, f);
		if (path_is_subpath(proj_source_root, path)) {
			obj new;
			make_obj(ctx->wk, &new, obj_file);
			*get_obj_file(ctx->wk, new) = f;
			obj_array_push(ctx->wk, files, new);
		}
	}

	return xc_project_target_sources(ctx, proj, 0, make_str(ctx->wk, "meson"), files);
}

static obj
xc_project_main_group(struct xc_ctx *ctx, struct project *proj)
{
	obj pbx, pbx_children;
	pbx = xc_pbx_new_group(ctx, &pbx_children);
	xc_pbx_push_kv(ctx, pbx, "sourceTree", xc_quoted_str(ctx, "<group>"));

	obj tgt;
	obj_array_for(ctx->wk, proj->targets, tgt) {
		xc_pbx_push_v(ctx, pbx_children, xc_project_target_group(ctx, proj, tgt));
	}

	xc_pbx_push_v(ctx, pbx_children, xc_project_meson_group(ctx, proj));

	return xc_pbx_push(ctx, ctx->pbx_objects, xc_obj_uuid(ctx, pbx, 0), pbx, 0);
}

static obj
xc_build_configuration_list(struct xc_ctx *ctx, struct project *proj, struct obj_build_target *tgt)
{
	obj pbx_build_settings = xc_pbx_new(ctx, obj_dict);
	xc_pbx_push_kv(ctx, pbx_build_settings, "ARCHS", xc_quoted_str(ctx, "arm64"));
	xc_pbx_push_kv(ctx, pbx_build_settings, "BUILD_DIR", xc_quoted(ctx, proj->build_root));
	xc_pbx_push_kv(ctx, pbx_build_settings, "BUILD_ROOT", xc_quoted_str(ctx, "$(BUILD_DIR)"));
	xc_pbx_push_kv(ctx, pbx_build_settings, "ONLY_ACTIVE_ARCH", xc_bool(ctx, true));
	xc_pbx_push_kv(ctx, pbx_build_settings, "MACOSX_DEPLOYMENT_TARGET", make_str(ctx->wk, "10.15"));
	/* xc_pbx_push_kv(ctx, pbx_build_settings, "SDKROOT", xc_bool(ctx, true)); */
	/* xc_pbx_push_kv(ctx, pbx_build_settings, "OBJROOT", xc_bool(ctx, true)); */

	if (tgt) {
		obj pbx_header_search_paths = xc_pbx_new(ctx, obj_array);
		obj v;
		obj_array_for(ctx->wk, tgt->dep_internal.include_directories, v) {
			struct obj_include_directory *i = get_obj_include_directory(ctx->wk, v);
			// TODO: ignoring system paths
			xc_pbx_push_v(ctx, pbx_header_search_paths, xc_quoted(ctx, i->path));
		}
		xc_pbx_push_v(ctx, pbx_header_search_paths, xc_quoted_str(ctx, "$(inherited)"));
		xc_pbx_push_kv(ctx, pbx_build_settings, "HEADER_SEARCH_PATHS", pbx_header_search_paths);


		obj pbx_gcc_preprocessor_definitions = xc_pbx_new(ctx, obj_array);

		obj _lang, args;
		(void)_lang;
		// TODO: This adds duplicates
		obj_dict_for(ctx->wk, tgt->args, _lang, args) {
			obj_array_for(ctx->wk, args, v) {
				const struct str *s = get_str(ctx->wk, v);
				if (str_startswith(s, &WKSTR("-D")) && s->len > 2) {
					xc_pbx_push_v(ctx, pbx_gcc_preprocessor_definitions, xc_quoted(ctx, make_strn(ctx->wk, s->s + 2, s->len - 2)));
				}
			}
		}

		xc_pbx_push_v(ctx, pbx_gcc_preprocessor_definitions, xc_quoted_str(ctx, "$(inherited)"));
		xc_pbx_push_kv(ctx, pbx_build_settings, "GCC_PREPROCESSOR_DEFINITIONS", pbx_gcc_preprocessor_definitions);
	}

	obj pbx_configuration = xc_pbx_new_t(ctx, "XCBuildConfiguration");
	xc_pbx_push_kv(ctx, pbx_configuration, "name", make_str(ctx->wk, "debug"));
	xc_pbx_push_kv(ctx, pbx_configuration, "buildSettings", pbx_build_settings);
	obj pbx_configuration_uuid = xc_pbx_push_root_object(ctx, pbx_configuration);

	obj pbx_build_configurations = xc_pbx_new(ctx, obj_array);
	xc_pbx_push_v(ctx, pbx_build_configurations, pbx_configuration_uuid);

	obj pbx_configuration_list = xc_pbx_new_t(ctx, "XCConfigurationList");
	xc_pbx_push_kv(ctx, pbx_configuration_list, "defaultConfigurationIsVisible", make_number(ctx->wk, 0));
	xc_pbx_push_kv(ctx, pbx_configuration_list, "defaultConfigurationName", make_str(ctx->wk, "debug"));
	xc_pbx_push_kv(ctx, pbx_configuration_list, "buildConfigurations", pbx_build_configurations);

	return xc_pbx_push_root_object(ctx, pbx_configuration_list);
}

static const char *ninja_build_target_name = "build with ninja";

static bool
xc_project_ninja_build_scheme(struct workspace *_wk, void *_ctx, FILE *out)
{
	struct xc_ctx *ctx = _ctx;
	struct xml_writer *w = &ctx->xw;

	obj default_exe = 0;
	{
		obj tgt;
		obj_array_for(ctx->wk, ctx->proj->targets, tgt) {
			if (get_obj_type(ctx->wk, tgt) != obj_build_target) {
				continue;
			}

			struct obj_build_target *t = get_obj_build_target(ctx->wk, tgt);

			if (t->type != tgt_executable) {
				continue;
			}

			default_exe = t->build_path;
		}
	}

	if (!default_exe) {
		default_exe = make_str(ctx->wk, "/path/to/your/executable");
	}

	obj buildable_reference = xml_node_new(w, "BuildableReference");
	xml_node_push_attr(w, buildable_reference, "BuildableIdentifier", make_str(ctx->wk, "primary"));
	xml_node_push_attr(w, buildable_reference, "BlueprintIdentifier", ctx->legacy_target_uuid);
	xml_node_push_attr(w, buildable_reference, "BuildableName", make_str(ctx->wk, ninja_build_target_name));
	xml_node_push_attr(w, buildable_reference, "BlueprintName", make_str(ctx->wk, ninja_build_target_name));
	xml_node_push_attr(
		w, buildable_reference, "ReferencedContainer", make_strf(ctx->wk, "container:%s", ctx->proj_path));

	obj build_action_entry = xml_node_new(w, "BuildActionEntry");
	xml_node_push_attr(w, build_action_entry, "buildForTesting", xc_bool(ctx, true));
	xml_node_push_attr(w, build_action_entry, "buildForRunning", xc_bool(ctx, true));
	xml_node_push_attr(w, build_action_entry, "buildForProfiling", xc_bool(ctx, true));
	xml_node_push_attr(w, build_action_entry, "buildForArchiving", xc_bool(ctx, true));
	xml_node_push_attr(w, build_action_entry, "buildForAnalyzing", xc_bool(ctx, true));
	xml_node_push_child(w, build_action_entry, buildable_reference);

	obj build_action_entries = xml_node_new(w, "BuildActionEntries");
	xml_node_push_child(w, build_action_entries, build_action_entry);

	obj build_action = xml_node_new(w, "BuildAction");
	xml_node_push_attr(w, build_action, "parallelizeBuildables", xc_bool(ctx, true));
	xml_node_push_attr(w, build_action, "buildImplicitDependencies", xc_bool(ctx, true));
	xml_node_push_attr(w, build_action, "buildArchitectures", make_str(ctx->wk, "Automatic"));
	xml_node_push_child(w, build_action, build_action_entries);

	obj path_runnable = xml_node_new(w, "PathRunnable");
	xml_node_push_attr(w, path_runnable, "runnableDebuggingMode", make_str(ctx->wk, "0"));
	xml_node_push_attr(w, path_runnable, "filePath", default_exe);

	obj launch_action = xml_node_new(w, "LaunchAction");
	xml_node_push_attr(w, launch_action, "buildConfiguration", make_str(ctx->wk, "debug"));
	xml_node_push_attr(w,
		launch_action,
		"selectedDebuggerIdentifier",
		make_str(ctx->wk, "Xcode.DebuggerFoundation.Debugger.LLDB"));
	xml_node_push_attr(w,
		launch_action,
		"selectedLauncherIdentifier",
		make_str(ctx->wk, "Xcode.DebuggerFoundation.Launcher.LLDB"));
	xml_node_push_attr(w, launch_action, "launchStyle", make_str(ctx->wk, "0"));
	xml_node_push_attr(w, launch_action, "useCustomWorkingDirectory", xc_bool(ctx, false));
	xml_node_push_attr(w, launch_action, "ignoresPersistentStateOnLaunch", xc_bool(ctx, false));
	xml_node_push_attr(w, launch_action, "debugDocumentVersioning", xc_bool(ctx, true));
	xml_node_push_attr(w, launch_action, "debugServiceExtension", make_str(ctx->wk, "internal"));
	xml_node_push_attr(w, launch_action, "allowLocationSimulation", xc_bool(ctx, true));
	xml_node_push_child(w, launch_action, path_runnable);

	obj root = xml_node_new(w, "Scheme");
	xml_node_push_attr(w, root, "version", make_str(ctx->wk, "1.7"));
	xml_node_push_child(w, root, build_action);
	xml_node_push_child(w, root, launch_action);

	xml_write(w, root, out);
	return true;
}

static obj
xc_project_ninja_build(struct xc_ctx *ctx, struct project *proj)
{
	obj pbx = xc_pbx_new_t(ctx, "PBXLegacyTarget");
	xc_pbx_push_kv(ctx, pbx, "buildArgumentsString", xc_quoted_str(ctx, "samu"));
	xc_pbx_push_kv(ctx, pbx, "buildConfigurationList", xc_build_configuration_list(ctx, proj, 0));

	xc_pbx_push_kv(ctx, pbx, "buildPhases", xc_pbx_new(ctx, obj_array));
	xc_pbx_push_kv(ctx, pbx, "buildToolPath", make_str(ctx->wk, ctx->wk->argv0));
	xc_pbx_push_kv(ctx, pbx, "buildWorkingDirectory", proj->build_root);
	xc_pbx_push_kv(ctx, pbx, "dependencies", xc_pbx_new(ctx, obj_array));
	xc_pbx_push_kv(ctx, pbx, "name", xc_quoted_str(ctx, ninja_build_target_name));
	xc_pbx_push_kv(ctx, pbx, "packageProductDependencies", xc_pbx_new(ctx, obj_array));
	xc_pbx_push_kv(ctx, pbx, "passBuildSettingsInEnvironment", make_number(ctx->wk, 1));
	xc_pbx_push_kv(ctx, pbx, "productName", xc_quoted_str(ctx, ninja_build_target_name));

	ctx->legacy_target_uuid = xc_pbx_push_root_object(ctx, pbx);

	SBUF(scheme_path);
	path_join(ctx->wk, &scheme_path, ctx->proj_path, "xcshareddata");
	path_push(ctx->wk, &scheme_path, "xcschemes");
	if (!fs_mkdir_p(scheme_path.buf)) {
		UNREACHABLE;
	}
	path_push(ctx->wk, &scheme_path, "autogenerated by muon.xcscheme");
	if (!with_open("", scheme_path.buf, ctx->wk, ctx, xc_project_ninja_build_scheme)) {
		UNREACHABLE;
	}

	return ctx->legacy_target_uuid;
}

static obj
xc_project_target(struct xc_ctx *ctx, struct project *proj, obj _tgt)
{
	obj pbx_build_uuid;
	if (!obj_dict_geti(ctx->wk, ctx->tgt_build_files, _tgt, &pbx_build_uuid)) {
		UNREACHABLE;
	}
	struct obj_build_target *tgt = get_obj_build_target(ctx->wk, _tgt);

	obj pbx_build_phases = xc_pbx_new(ctx, obj_array);
	xc_pbx_push_v(ctx, pbx_build_phases, pbx_build_uuid);

	obj pbx = xc_pbx_new_t(ctx, "PBXNativeTarget");
	xc_pbx_push_kv(ctx, pbx, "buildConfigurationList", xc_build_configuration_list(ctx, proj, tgt));
	xc_pbx_push_kv(ctx, pbx, "buildPhases", pbx_build_phases);
	xc_pbx_push_kv(ctx, pbx, "buildRules", xc_pbx_new(ctx, obj_array));
	xc_pbx_push_kv(ctx, pbx, "dependencies", xc_pbx_new(ctx, obj_array));
	xc_pbx_push_kv(ctx, pbx, "name", tgt->build_name);
	xc_pbx_push_kv(ctx, pbx, "productName", tgt->build_name);

	return xc_pbx_push_root_object(ctx, pbx);
}

static bool
xc_project_main(struct workspace *_wk, void *_ctx, FILE *_out)
{
	struct xc_ctx *ctx = _ctx;
	ctx->out = _out;
	struct project *proj = ctx->proj;

	make_obj(ctx->wk, &ctx->tgt_build_files, obj_dict);

	obj pbx_main = xc_pbx_new(ctx, obj_dict);
	xc_pbx_push_kv(ctx, pbx_main, "archiveVersion", make_number(ctx->wk, 1));
	xc_pbx_push_kv(ctx, pbx_main, "classes", xc_pbx_new(ctx, obj_dict));
	xc_pbx_push_kv(ctx, pbx_main, "objectVersion", make_number(ctx->wk, 49));

	obj pbx_objects = ctx->pbx_objects = xc_pbx_new(ctx, obj_dict);
	xc_pbx_push_kv(ctx, pbx_main, "objects", pbx_objects);

	{
		obj pbx_attributes = xc_pbx_new(ctx, obj_dict);
		xc_pbx_push_kv(ctx, pbx_attributes, "BuildIndependentTargetsInParallel", xc_bool(ctx, true));

		obj pbx_known_regions = xc_pbx_new(ctx, obj_array);
		// TODO: ???
		xc_pbx_push_v(ctx, pbx_known_regions, make_strf(ctx->wk, "en"));

		obj pbx = xc_pbx_new_t(ctx, "PBXProject");
		xc_pbx_push_kv(ctx, pbx, "attributes", pbx_attributes);
		xc_pbx_push_kv(ctx, pbx, "projectDirPath", xc_quoted(ctx, proj->source_root));
		xc_pbx_push_kv(ctx, pbx, "projectRoot", xc_quoted_str(ctx, ""));
		xc_pbx_push_kv(ctx, pbx, "buildConfigurationList", xc_build_configuration_list(ctx, proj, 0));
		xc_pbx_push_kv(ctx, pbx, "mainGroup", xc_project_main_group(ctx, proj));
		/* xc_pbx_push_kv(ctx, pbx, "compatibilityVersion", make_str(ctx->wk, "Xcode 15.0")); */
		/* xc_pbx_push_kv(ctx, pbx, "hasScannedForEncodings", make_number(ctx->wk, 0)); */
		xc_pbx_push_kv(ctx, pbx, "knownRegions", pbx_known_regions);

		obj pbx_targets = xc_pbx_new(ctx, obj_array), tgt;
		obj_array_for(ctx->wk, proj->targets, tgt) {
			if (get_obj_type(ctx->wk, tgt) == obj_build_target) {
				xc_pbx_push_v(ctx, pbx_targets, xc_project_target(ctx, proj, tgt));
			}
		}

		if (ctx->master_project) {
			xc_pbx_push_v(ctx, pbx_targets, xc_project_ninja_build(ctx, proj));
		}

		xc_pbx_push_kv(ctx, pbx, "targets", pbx_targets);

		xc_pbx_push_kv(ctx, pbx_main, "rootObject", xc_pbx_push_root_object(ctx, pbx));
	}

	// write it out
	fprintf(ctx->out, "// !$*UTF8*$!\n");
	xc_pbx_write(ctx, pbx_main);
	fprintf(ctx->out, "\n");
	return true;
}

static bool
xc_main(struct workspace *_wk, void *_ctx, FILE *workspace_out)
{
	struct xc_ctx *ctx = _ctx;
	struct xml_writer *w = &ctx->xw;

	obj xcworkspace = xml_node_new(w, "Workspace");
	xml_node_push_attr(w, xcworkspace, "version", make_str(ctx->wk, "1.0"));

	uint32_t i;
	for (i = 0; i < ctx->wk->projects.len; ++i) {
		struct project *proj = ctx->proj = arr_get(&ctx->wk->projects, i);
		ctx->master_project = i == 0;

		SBUF(proj_name);
		sbuf_pushf(ctx->wk, &proj_name, "%s.xcodeproj", get_cstr(ctx->wk, proj->cfg.name));
		SBUF(proj_path);
		path_push(ctx->wk, &proj_path, ctx->wk->build_root);
		path_push(ctx->wk, &proj_path, "xcode-projects");
		path_push(ctx->wk, &proj_path, proj_name.buf);
		if (!fs_mkdir_p(proj_path.buf)) {
			return false;
		}

		ctx->proj_path = proj_path.buf;

		obj xcworkspace_file_ref = xml_node_new(w, "FileRef");
		xml_node_push_attr(w, xcworkspace_file_ref, "location", make_strf(ctx->wk, "container:%s", proj_path.buf));
		xml_node_push_child(w, xcworkspace, xcworkspace_file_ref);

		SBUF(pbx_path);
		path_join(ctx->wk, &pbx_path, proj_path.buf, "project.pbxproj");

		ctx->pbx_path = pbx_path.buf;

		if (!with_open("", pbx_path.buf, ctx->wk, ctx, xc_project_main)) {
			return false;
		}
	}

	xml_write(w, xcworkspace, workspace_out);
	return true;
}

bool
xcode_write_all(struct workspace *wk)
{
	SBUF(xcworkspace_path);
	path_join(wk, &xcworkspace_path, wk->build_root, "main.xcworkspace");
	if (!fs_mkdir_p(xcworkspace_path.buf)) {
		return false;
	}
	path_push(wk, &xcworkspace_path, "contents.xcworkspacedata");

	struct xc_ctx ctx = { .wk = wk };
	bucket_arr_init(&ctx.pbx_item, sizeof(struct pbx_item), 1024);
	stack_init(&ctx.stack, 4096);
	xml_writer_init(wk, &ctx.xw);

	bool ok = with_open("", xcworkspace_path.buf, wk, &ctx, xc_main);

	bucket_arr_destroy(&ctx.pbx_item);
	stack_destroy(&ctx.stack);
	xml_writer_destroy(&ctx.xw);

	return ok;
}
