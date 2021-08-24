src = files([
	'args.c',
	'coerce.c',
	'compilers.c',
	'data/bucket_array.c',
	'data/darr.c',
	'data/hash.c',
	'error.c',
	'formats/ini.c',
	'formats/tar.c',
	'functions/array.c',
	'functions/build_target.c',
	'functions/common.c',
	'functions/compiler.c',
	'functions/configuration_data.c',
	'functions/custom_target.c',
	'functions/default.c',
	'functions/default/configure_file.c',
	'functions/default/dependency.c',
	'functions/default/options.c',
	'functions/default/setup.c',
	'functions/dependency.c',
	'functions/dict.c',
	'functions/external_library.c',
	'functions/external_program.c',
	'functions/feature_opt.c',
	'functions/file.c',
	'functions/machine.c',
	'functions/meson.c',
	'functions/modules.c',
	'functions/modules/fs.c',
	'functions/number.c',
	'functions/run_result.c',
	'functions/string.c',
	'functions/subproject.c',
	'lang/eval.c',
	'lang/interpreter.c',
	'lang/lexer.c',
	'lang/object.c',
	'lang/parser.c',
	'lang/serial.c',
	'lang/workspace.c',
	'log.c',
	'machine_file.c',
	'main.c',
	'opts.c',
	'output/output.c',
	'platform/dirs.c',
	'platform/filesystem.c',
	'platform/mem.c',
	'platform/path.c',
	'platform/run_cmd.c',
	'sha_256.c',
	'tests.c',
	'wrap.c',
]) + [configure_file(
	configuration: version_info,
	input: 'version.c.in',
	output: 'version.c'
)]

foreach d: [
		['libcurl',    'curl',       'dependency'],
		['zlib',       'zlib',       'dependency'],
		['libpkgconf', 'libpkgconf', 'dependency'],
		['samurai',    'samu',       'subproject', 'libsamu_dep'],
	]

	found = false

	if d[2] == 'subproject'
		if not get_option(d[1]).disabled()
			sub = subproject(d[0])
			dep = sub.get_variable(d[3])
			found = true
		endif
	else
		dep = dependency(d[0], required: get_option(d[1]), static: get_option('static'))
		found = dep.found()
	endif

	if found
		deps += [dep]
		src += [files(['external/' + d[1] + '.c'])]
	else
		src += [files(['external/' + d[1] + '_null.c'])]
	endif
endforeach
