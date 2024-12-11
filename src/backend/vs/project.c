/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "backend/backend.h"
#include "backend/vs.h"
#include "backend/vs/xml.h"
#include "error.h"
#include "options.h"
#include "platform/path.h"

/* https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-file-structure?view=msvc-170 */

struct target_source_iter_ctx
{
	struct xml_node parent;
	struct arr attributes; /* array of xml_attributes */
};

struct target_include_directory_iter_ctx
{
	struct sbuf *buf;
};

static enum iteration_result
write_target_sources_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct target_source_iter_ctx *ctx = _ctx;
	const char *src = get_file_path(wk, val);

	printf(" %%%% source: '%s'\n", src);
	fflush(stdout);

	SBUF(path);
	path_relative_to(wk, &path, wk->build_root, src);
	struct arr attributes; /* array of strings */
	arr_init(&attributes, 1, sizeof(struct xml_attribute));
	ATTR_PUSH("Include", path.buf);
	struct xml_node n = tag(wk, ctx->parent, "ClCompile", &attributes, true);
	tag_end(wk, n);
	return ir_cont;
}

static enum iteration_result
write_target_include_directory_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct target_include_directory_iter_ctx *ctx = _ctx;

	switch (get_obj_type(wk, val)) {
	case obj_include_directory:
	{
		struct obj_include_directory *incdir = get_obj_include_directory(wk, val);
		SBUF(dir);
		path_relative_to(wk, &dir, wk->build_root, get_cstr(wk, incdir->path));
		sbuf_pushs(0, ctx->buf, dir.buf);
		sbuf_push(0, ctx->buf, ';');
		break;
	default:
		break;//UNREACHABLE;
	}
	}

	return ir_cont;
}

bool
vs_write_project(struct workspace *wk, void *_ctx, FILE *out)
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

	/* Project Configuration */
	arr_clear(&attributes);
	ATTR_PUSH("Label", "ProjectConfigurations");
	struct xml_node n1 = tag(wk, root, "ItemGroup", &attributes, false);
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		for (uint32_t j = 0; j < ARRAY_LEN(vs_platforms); j++) {
			SBUF_manual(buf);
			sbuf_pushf(wk, &buf, "%s|%s", vs_configurations[i], vs_machines[j]);
			arr_clear(&attributes);
			ATTR_PUSH("Include", buf.buf);
			struct xml_node n2 = tag(wk, n1, "ProjectConfiguration", &attributes, false);
			tag_elt(n2, "Configuration", vs_configurations[i]);
			tag_elt(n2, "Platform", vs_machines[j]);
			tag_end(wk, n2);
			sbuf_destroy(&buf);
		}
	}
	tag_end(wk, n1);

	/* Property Group: Globals */
	struct obj_build_target *target = get_obj_build_target(wk, ctx->tgt_id);
	arr_clear(&attributes);
	ATTR_PUSH("Label", "Globals");
	n1 = tag(wk, root, "PropertyGroup", &attributes, false);

	SBUF_manual(buf);
	sbuf_pushf(0, &buf, "{%04X}", ctx->tgt_id);
	tag_elt(n1, "ProjectGuid", buf.buf);
	sbuf_destroy(&buf);
	tag_elt(n1, "VCProjectVersion", ctx->vs_version == 16 ? "16.0" : "17.0");
	tag_elt(n1, "RootNamespace", get_cstr(wk, target->name));
	tag_elt(n1, "ProjectName", get_cstr(wk, target->name));
	tag_elt(n1, "Keyword", "x64Proj");
	tag_elt(n1, "Platform", "x64");

	tag_end(wk, n1);

	/* Import */
	arr_clear(&attributes);
	ATTR_PUSH("Project", "$(VCTargetsPath)\\Microsoft.Cpp.Default.props");
	n1 = tag(wk, root, "Import", &attributes, true);
	tag_end(wk, n1);

	/* Property Group: Configuration */
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		const char *debug_lib;
		bool whole_opt = false;
		if (strcmp(vs_configurations[i], "Debug") == 0) {
			debug_lib = "true";
		} else {
			debug_lib = "false";
			whole_opt = true;
		}

		for (uint32_t j = 0; j < ARRAY_LEN(vs_platforms); j++) {
			arr_clear(&attributes);
			sbuf_clear(&buf);
			sbuf_pushf(0, &buf, "'$(Configuration)|$(Platform)'=='%s|%s'", vs_configurations[i], vs_machines[j]);
			ATTR_PUSH("Condition", buf.buf);
			ATTR_PUSH("Label", "Configuration");
			n1 = tag(wk, root, "PropertyGroup", &attributes, false);
			sbuf_destroy(&buf);

			const char *conf_type;
			switch (target->type) {
			case tgt_executable:
				conf_type = "Application";
				break;
			case tgt_static_library:
				conf_type = "StaticLibrary";
				break;
			case tgt_dynamic_library:
				conf_type = "DynamicLibrary";
				break;
			case tgt_shared_module:
				conf_type = "DynamicLibrary";
				break;
			default: UNREACHABLE;
			}
			tag_elt(n1, "ConfigurationType", conf_type);
			tag_elt(n1, "UseDebugLibraries", debug_lib);
			tag_elt(n1, "PlatformToolset", ctx->vs_version == 16 ? "v142" : "v143");
			if (whole_opt) {
				tag_elt(n1, "WholeProgramOptimization", "true");
			}
			tag_elt(n1, "CharacterSet", "MultiByte");

			tag_end(wk, n1);
		}
	}

	/* Import */
	arr_clear(&attributes);
	ATTR_PUSH("Project", "$(VCTargetsPath)\\Microsoft.Cpp.props");
	n1 = tag(wk, root, "Import", &attributes, true);
	tag_end(wk, n1);

	/* import group - ExtensionSettings */
	arr_clear(&attributes);
	ATTR_PUSH("Label", "ExtensionSettings");
	n1 = tag(wk, root, "ImportGroup", &attributes, false);
	tag_end(wk, n1);

	/* import group - Shared */
	arr_clear(&attributes);
	ATTR_PUSH("Label", "Shared");
	n1 = tag(wk, root, "ImportGroup", &attributes, false);
	tag_end(wk, n1);

	/* import group - PropertySheets */
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		for (uint32_t j = 0; j < ARRAY_LEN(vs_platforms); j++) {
			arr_clear(&attributes);
			ATTR_PUSH("Label", "PropertySheets");
			sbuf_clear(&buf);
			sbuf_pushf(0, &buf, "'$(Configuration)|$(Platform)'=='%s|%s'", vs_configurations[i], vs_machines[j]);
			ATTR_PUSH("Condition", buf.buf);
			n1 = tag(wk, root, "ImportGroup", &attributes, false);

			arr_clear(&attributes);
			ATTR_PUSH("Project", "$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props");
			ATTR_PUSH("Condition", "exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')");
			ATTR_PUSH("Label", "LocalAppDataPlatform");
			struct xml_node n2 = tag(wk, n1, "Import", &attributes, true);
			tag_end(wk, n2);

			tag_end(wk, n1);
		}
	}

	/* property group - UserMacros */
	arr_clear(&attributes);
	ATTR_PUSH("Label", "UserMacros");
	n1 = tag(wk, root, "PropertyGroup", &attributes, true);
	tag_end(wk, n1);

	/* item definition group - compiler and linker */
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		bool is_debug = strcmp(vs_configurations[i], "Debug") == 0;
		for (uint32_t j = 0; j < ARRAY_LEN(vs_machines); j++) {
			arr_clear(&attributes);
			sbuf_clear(&buf);
			sbuf_pushf(0, &buf, "'$(Configuration)|$(Platform)'=='%s|%s'", vs_configurations[i], vs_machines[j]);
			ATTR_PUSH("Condition", buf.buf);
			n1 = tag(wk, root, "ItemDefinitionGroup", &attributes, false);

			struct xml_node n2;

			/* compiler */
			n2 = tag(wk, n1, "ClCompile", NULL, false);

			// Warnings as error
			obj active;
			get_option_value_overridable(wk, ctx->project, target->override_options, "werror", &active);
			if (get_obj_bool(wk, active)) {
				tag_elt(n2, "TreatWarningAsError", "true");
			}

			// Warning level
			obj level_id;
			get_option_value_overridable(wk, ctx->project, target->override_options, "warning_level", &level_id);
			const struct str *sl = get_str(wk, level_id);
			if (str_eql(sl, &WKSTR("everything"))) {
				tag_elt(n2, "WarningLevel", "EnableAllWarnings");
			} else {
				assert(sl->len == 1 && "invalid warning_level");
				switch (sl->s[0]) {
				case '1': {
					tag_elt(n2, "WarningLevel", "Level2");
					break;
				}
				case '2': {
					tag_elt(n2, "WarningLevel", "Level3");
					break;
				}
				case '3': {
					tag_elt(n2, "WarningLevel", "Level4");
					break;
				}
				// default: nothing added
				default: break;
				}
			}

			// SDL check
			// FIXME: how to get it ??
			if (!is_debug) {
				tag_elt(n2, "FunctionLevelLinking", "true");
				tag_elt(n2, "IntrinsicFunctions", "true");
			}
			tag_elt(n2, "SDLCheck", "true");

			// Preprocessor definitions
			sbuf_clear(&buf);
			if (strcmp(vs_machines[j], "Win32") == 0) {
				sbuf_pushs(0, &buf, "WIN32;");
			}
			sbuf_pushf(0, &buf, "%s;", is_debug ? "_DEBUG" : "NDEBUG");
			sbuf_pushs(0, &buf, "_CONSOLE;%(PreprocessorDefinitions)");
			tag_elt(n2, "PreprocessorDefinitions", buf.buf);

			// Conformance mode
			tag_elt(n2, "ConformanceMode", "true");

			// Additional include directories
			if (target->dep_internal.include_directories) {
				struct target_include_directory_iter_ctx ctx_incdir;

				sbuf_clear(&buf);
				ctx_incdir.buf = &buf;
				obj_array_foreach(wk, target->dep_internal.include_directories, &ctx_incdir, write_target_include_directory_iter);
				sbuf_pushs(0, &buf, "%(AdditionalIncludeDirectories)");
				tag_elt(n2, "AdditionalIncludeDirectories", buf.buf);
			}

			tag_end(wk, n2);

			/* linker */
			n2 = tag(wk, n1, "Link", NULL, false);
			// FIXME: is it implemented ?
			tag_elt(n2, "SubSystem", "Console");
			if (!is_debug) {
				tag_elt(n2, "EnableCOMDATFolding", "true");
				tag_elt(n2, "OptimizeReferences", "true");
			}
			tag_elt(n2, "GenerateDebugInformation", "true");
			tag_end(wk, n2);

			tag_end(wk, n1);
		}
	}

	/* muon seems to keep track of source files only, no header files */
	// FIXME: no source file for shared lib
	n1 = tag(wk, root, "ItemGroup", NULL, false);
	struct target_source_iter_ctx ctx_sources = { n1, attributes };
	if (!obj_array_foreach(wk, target->src, &ctx_sources, write_target_sources_iter)) {
		return false;
	}
	tag_end(wk, n1);

	/* Import */
	arr_clear(&attributes);
	ATTR_PUSH("Project", "$(VCTargetsPath)\\Microsoft.Cpp.targets");
	n1 = tag(wk, root, "Import", &attributes, true);
	tag_end(wk, n1);

	arr_clear(&attributes);
	ATTR_PUSH("Label", "ExtensionTargets");
	n1 = tag(wk, root, "ImportGroup", &attributes, false);
	tag_end(wk, n1);

	tag_end(wk, root);

	return true;
}
