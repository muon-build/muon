# sources

src = files([
	'coerce.c',
	'darr.c',
	'eval.c',
	'external/microtar.c',
	'external/sha-256.c',
	'filesystem.c',
	'functions/common.c',
	'functions/compiler.c',
	'functions/configuration_data.c',
	'functions/default.c',
	'functions/default/configure_file.c',
	'functions/default/dependency.c',
	'functions/default/options.c',
	'functions/dependency.c',
	'functions/dict.c',
	'functions/external_library.c',
	'functions/external_program.c',
	'functions/feature_opt.c',
	'functions/machine.c',
	'functions/meson.c',
	'functions/number.c',
	'functions/run_result.c',
	'functions/string.c',
	'functions/subproject.c',
	'hash.c',
	'inih.c',
	'interpreter.c',
	'lexer.c',
	'log.c',
	'main.c',
	'mem.c',
	'object.c',
	'opts.c',
	'output.c',
	'parser.c',
	'run_cmd.c',
	'tests.c',
	'workspace.c',
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
			sub = subproject(d[1])
			dep = sub.get_variable(d[3])
			found = true
		endif
	else
		dep = dependency(d[0], required: get_option(d[1]))
		found = dep.found()
	endif

	if found
		deps += [dep]
		src += [files(['external/' + d[1] + '.c'])]
	else
		src += [files(['external/' + d[1] + '_null.c'])]
	endif
endforeach
