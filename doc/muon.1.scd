muon(1)

; SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
; SPDX-License-Identifier: GPL-3.0-only

# NAME

muon - a meson-compatible build system

# SYNOPSIS

*muon* [*-vh*] [*-C* <chdir>] <command> [<args>]

*muon* *setup* [*-D*[subproject*:*]option*=*value...] build ++
*cd* build++
<invoke backend build tool>++
*muon* *test* [options] ++
*muon* *install* [options]

# DESCRIPTION

*muon* interprets _source files_ written in the _meson dsl_ and produces
_buildfiles_ for a backend.  Currently the only supported backend is _ninja_.

When building *meson* projects with *muon*, you typically first start by
running the *setup* command in the project root.  This will create _buildfiles_
for the backend in the _build dir_ you specify.  You then invoke the backend,
e.g.

```
ninja -C <build dir>
```

If the project defines tests, you may run them with the *test* subcommand, and
finally install the project with the *install* subcommand.

# OPTIONS

- *-v* - enable verbose output
- *-C* <path> - chdir to _path_ before executing a command
- *-h* - print a help message

# COMMANDS

*muon* requires a command.

All commands accept a *-h* option which prints a brief summary of their usage.

## analyze
	*muon* *analyze*

	Run a static analyzer on the current project.

	*OPTIONS*:
	- *-l* - optimize output for editor linter plugins.  For example, a
	  diagnostic is added for the subdir() callsite, so that the editor can
	  highlight subdirs with errors in them.
	- *-q* - only report errors
	- *-u* - error on unused variables, the default is to warn only.
	- *-O* <path> - read project file with matching path from stdin.  This
	  is useful for editor linter plugins that run as you type on files
	  that aren't saved to disk.
	- *-W* [no-]<diagnostic> - enable or disable a particular diagnostic,
	e.g. unused-variable.
	- *-W* list - list available diagnostics.

## benchmark
	See documentation for the *test* subcommand.

## check
	*muon* *check* [*-p*|*-P*] <filename>

	Check if _filename_ parses.

	*OPTIONS*:
	- *-p* - print the parsed ast
	- *-P* - print the parsed formatting ast

## fmt
	*muon* *fmt* [*-i*] [*-q*] [*-c* <muon_fmt.ini>] <filename>

	Format a _source file_.  The formatting is currently minimally
	configurable, and is based on the official meson style guide
	<https://mesonbuild.com/Style-guide.html>.

	*OPTIONS*:
	- *-q* - exit with 1 if the file would be changed by muon fmt
	- *-i* - format file in-place
	- *-c* <muon_fmt.ini> - read configuration from _muon\_fmt.ini_.

	*CONFIGURATION OPTIONS*
[[ *key*
:[ *type*
:[ *default*
:[ *description*
|  max_line_len
:  uint
:  80
:  The maximum length of lines before they are split
|  indent_by
:  str
:  '    '
:  A string that will be used to indent with
|  space_array
:  bool
:  false
:  Whether to include spaces around array elements (ex. [1] vs [ 1 ])
|  kwargs_force_multiline
:  bool
:  false
:  Make every dictionary literal and function call with kwargs expand into
   multiple lines.
|  wide_colon
:  bool
:  false
:  Whether to put a space before the colon operator (ex. `key : val`)
|  no_single_comma_function
:  bool
:  false
:  Don't add a trailing comma to multi-line function calls with only one
   argument.

## install
	*muon* *install* [*-n*]

	Installs the project. The _DESTDIR_ environment variable is respected
	and will prefix all installation directories if it is present.

	*OPTIONS*:
	- *-n* - dry run

## internal
	*muon* *internal* <command> [<args>]

	Internal contains several subcommands used by *muon* internally.  These
	commands are subject to change at any time and this should not be
	relied upon.

	*SUBCOMMANDS*:
	- *eval* - evaluate a _source file_
	- *exe* - execute a command
	- *repl* - start a _meson dsl_ repl
	- *dump_funcs* - output all supported functions and arguments

## internal eval
	*muon* *internal* *eval* [*-e*] [*-s*] <filename> [<args>]

	Interpret a _source file_.  The interpreter environment is
	substantially different from the typical environment during *setup*.
	Build related functions are unavailable, as well as many other
	functions including *subdir*.  Additionally, the variable *argv* is
	defined as an array containing the commandline starting at <filename>.

	*OPTIONS*:
	- *-e* - lookup <filename> as an embedded script
	- *-s* - disable functions that are unsafe to be called at random,
	particularly `run_command()`.  The motivation for this flag is so that
	automated fuzz testing can be used without accidentally executing
	something like `run_command('rm', '-rf', '/')`.

## internal exe
	*muon* *internal* *exe* [*-f* <input file>] [*-c* <output file>] [*-e*
	<env.dat>] [*-a* <args.dat>] [-- <cmd> [<args>]]

	Execute <cmd> with arguments <args>.

	*OPTIONS*:
	- *-f* <input file> - pass _input file_ as stdin to <cmd>
	- *-c* <output file> - capture stdout of <cmd> and write it to _output
	  file_
	- *-e* <env.dat> - read and set environment variables from _env.dat_
	- *-a* <args.dat> - read and set command from _args.dat_

## internal repl
	*muon* *internal* *repl*

	Start a _meson dsl_ repl.  The functions available are limited as with
	*internal eval*.

## internal dump_funcs
	*muon* *internal* *dump_funcs*

	Print all supported functions, methods, and module functions with their
	arguments, argument types, and return types to stdout.  This subcommand
	is mainly useful for generating https://muon.build/status.html.

## options
	*muon* *options* [*-a*] [*-m*]

	Lists available project options and defaults.  This command may either
	be run from the project root or from a build directory.  Running this
	command from a build directory highlights configured option values in
	the output, whereas running it from the project root causes the default
	value for each option to be highlighted.

	*OPTIONS*:
	- *-a* - Include builtin global and per-project options in the output.
	- *-m* - Only display option values that have been modified.

## samu
	*muon* *samu* [<args>]

	Executes an embedded copy of *samu*(1).  This command requires that muon
	was compiled with *samu* enabled.

## setup
	*muon* *setup* [*-D*[subproject*:*]option*=*value...] [*-c* <compiler
	check cache.dat>] [*-b*] <build dir>

	Interpret all _source files_ and generate _buildfiles_ in _build dir_.

	*OPTIONS*:
	- *-D* [subproject*:*]option*=*value - Set build options.  Options
	  are either built in or project-defined.  Subproject options can be
	  specified by prepending the subproject's name and a colon to the
	  *option*.  This option may be specified multiple times.
	- *-c* <path> - load compiler check cache dump from path.  This is used
	  internally when creating the regeneration command.
	- *-b* - Break on error.  When this option is passed, muon will enter a
	  debugging repl when a fatal error is encountered.  From there you can
	  inspect and modify state, and optionally continue setup.

## summary
	*muon* *summary*

	Print a previously configured project's summary.

## test
	*muon* *test* [*-d* <display mode>] [*-e* <setup>] [*-f*] [*-j* <jobs>]
	\[*-l*] [*-R*] [*-s* <suite>] [*-S*] [*-v [*-v*]*]

	Execute tests defined in _source files_.

	*OPTIONS*:
	- *-d* <display mode> - Control test output.  _display mode_ can be one
	  of *auto*, *dots*, or *bar*.  *dots* prints a '.' for success and 'E'
	  for error, *bar* prints a progress bar with an error count.  The
	  default mode, *auto*, selects *bar* if the output device is a
	  terminal or *dots* otherwise.
	- *-e* <setup> - Use test setup _setup_.
	- *-f* - Fail fast. exit after first test failure is encountered.
	- *-j* - Set the number of jobs used when running tests.
	- *-l* - List tests that would be run with the current setup, suites,
	etc.  The format of the output is <project name>:<list of suites> -
	<test_name>.
	- *-R* - No rebuild. Disable automatic build system invocation prior to
	  running tests.
	- *-s* <suite> - Only run tests in suite _suite_.  This option may be
	  specified multiple times.
	- *-S* - print a summary of test results, including the duration of
	  each test
	- *-v* - Increase verbosity.  When passed once, print test results as
	  they are completed.  When passed twice, the stdout/stderr of tests is
	  not captured.

## version
	*muon* *version*

	Print out version information as well as enabled features.

# SEE ALSO

meson.build(5) meson-reference(3) meson(1)

# AUTHORS

Maintained by Stone Tickle <lattis@mochiro.moe>, who is assisted by other open
source contributors.  For more information about muon development, see
<https://sr.ht/~lattis/muon>.
