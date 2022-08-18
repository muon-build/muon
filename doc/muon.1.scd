muon(1)

# NAME

muon - a meson-compatible build system

# SYNOPSIS

*muon* [*-vlh*] [*-C* <chdir>] <command> [<args>]

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
- *-l* - show source locations in log
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

## benchmark
	See documentation for the *test* subcommand.

## check
	*muon* *check* [*-p*] <filename>

	Check if _filename_ parses.

	*OPTIONS*:
	- *-p* - print the parsed ast

## fmt_unstable
	*muon* *fmt_unstable* [*-i*] [*-c* <muon_fmt.ini>] <filename>

	Format a _source file_.  The formatting is currently minimally
	configurable, and is based on the official meson style guide
	<https://mesonbuild.com/Style-guide.html>.

	Note: This command is suffixed by \_unstable because it is not ready
	for production use.  There are still issues around line breaking, and
	the formatting style has not been fully decided.

	*OPTIONS*:
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
|  kwa_ml
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

## internal eval
	*muon* *internal* *eval* <filename> [<args>]

	Interpret a _source file_.  The interpreter environment is
	substantially different from the typical environment during *setup*.
	Build related functions are unavailable, as well as many other
	functions including *subdir*.  Additionally, the variable *argv* is
	defined as an array containing the commandline starting at <filename>.

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

## info options
	*muon* *info* *options* [*-a*]

	Lists available project options and defaults

	*OPTIONS*:
	- *-a* - Include builtin global and per-project options in the output.

## info summary
	*muon* *info* *summary*

	Print a previously configured project's summary.

## samu
	*muon* *samu* [<args>]

	Executes an embedded copy of *samu*(1).  This command requires that muon
	was compiled with *samu* enabled.

## setup
	*muon* *setup* [*-D*[subproject*:*]option*=*value...] <build dir>

	Interpret all _source files_ and generate _buildfiles_ in _build dir_.

	*OPTIONS*:
	- *-D* [subproject*:*]option*=*value - Set build options.  Options
	  are either built in or project-defined.  Subproject options can be
	  specified by prepending the subproject's name and a colon to the
	  *option*.  This option may be specified multiple times.

## test
	*muon* *test* [*-S*] [*-f*] [*-w* <workers>] [*-s* <suite>] [*-d*
	<display mode>] [*-v [*-v*]*] [*-R*]

	Execute tests defined in _source files_.

	*OPTIONS*:
	- *-s* <suite> - Only run tests in suite _suite_.  This option may be
	  specified multiple times.
	- *-d* <display mode> - Control test output.  _display mode_ can be one
	  of *auto*, *dots*, or *bar*.  *dots* prints a '.' for success and 'E'
	  for error, *bar* prints a progress bar with an error count.  The
	  default mode, *auto*, selects *bar* if the output device is a
	  terminal or *dots* otherwise.
	- *-S* - print a summary of test results, including the duration of
	  each test
	- *-f* - Fail fast. exit after first test failure is encountered.
	- *-w* - Set the number of workers used when running tests.
	- *-v* - Increase verbosity.  When passed once, print test results as
	  they are completed.  When passed twice, the stdout/stderr of tests is
	  not captured.
	- *-R* - No rebuild. Disable automatic build system invocation prior to
	  running tests.

## version
	*muon* *version*

	Print out version information as well as enabled features.

# SEE ALSO

meson.build(5) meson-reference(3) meson(1)

# AUTHORS

Maintained by Stone Tickle <lattis@mochiro.moe>, who is assisted by other open
source contributors.  For more information about muon development, see
<https://sr.ht/~lattis/muon>.
