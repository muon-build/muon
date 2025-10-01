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
_buildfiles_ for a backend.

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
- *-q* - silence logging except for errors
- *-C* <path> - chdir to _path_ before executing a command
- *-h* - print a help message

# COMMANDS

*muon* requires a command.

All commands accept a *-h* option which prints a brief summary of their usage.

## analyze
	*muon* *analyze* [*-l*] [*-m* <mode>] [*-q*] [*-O* <path>]
	\[*-W* [no-]<diagnostic|list|error>] [command]

	Run a static analyzer on the current project.

	*OPTIONS*:
	- *-l* - optimize output for editor linter plugins.  For example, a
	  diagnostic is added for the subdir() callsite, so that the editor can
	  highlight subdirs with errors in them.
	- *-m* <mode> - set the language mode
	- *-q* - only report errors
	- *-O* <path> - read project file with matching path from stdin.  This
	  is useful for editor linter plugins that run as you type on files
	  that aren't saved to disk.
	- *-W* [no-]<diagnostic> - enable or disable a particular diagnostic,
	  e.g. unused-variable.
	- *-W* list - list available diagnostics.
	- *-W* error - turn all warnings into errors.

	*COMMANDS*:
	- *trace* - print a tree of all meson source files that are evaluated
	- *lsp* - run the analyzer as a language server
	- *root-for* <path> - given the path to a file, try to determine the
	  project's root
	- *file* <path> - run the analyzer in single file mode.  Implies -m module,
	  but -m script can be passed to analyze a file in script mode.  This
	  is useful for catching bugs in scripts that will be evaluated with
	  *muon internal eval*, or script modules.

## build
	*muon* *build* [setup options] <build dir|source dir> [ninja options]

	Invoke muon setup + ninja in a single command.  Additionally setup will only
	run if the passed options have changed, including options implicitly set
	using environment variabls.

## fmt
	*muon* *fmt* [*-i*] [*-q*] [*-l*] [*-c* <muon_fmt.ini>] <file>[ <file>[...]]

	Format a _source file_.

	*OPTIONS*:
	- *-q* - exit with 1 if files would be modified by muon fmt
	- *-l* - like -q but also print failing filenames
	- *-i* - format files in-place
	- *-c* <muon_fmt.ini> - read configuration from _muon\_fmt.ini_
	- *-e* - try to read configuration from .editorconfig.  Only indentation
	  related settings are recognized.

	*CONFIGURATION OPTIONS*
[[ *key*
:[ *type*
:[ *default*
:[ *description*
|  max_line_len
:  int
:  80
:  The maximum length of lines before they are split
|  indent_style
:  space|tab
:  space
:  Wether to use spaces or tabs for indentation
|  indent_size
:  int
:  4
:  For indent_style = space, the number of spaces to indent by per level
|  insert_final_newline
:  bool
:  true
:  Whether or not to insert a trailing newline at the end of the file
|  end_of_line
:  lf|crlf|cr
:  <unset>
:  The type of line ending to use.  If this value is not set then the
   line ending of the first line in the source file will be used.
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
|  sort_files
:  bool
:  true
:  Whether to sort the arguments of files()
|  group_arg_value
:  bool
:  true
:  Whether to group strings in array literals such that command line arguments
   that take a value remain on the same line as the value.  The heuristic used
   is that if a string starts with `-` and the next string does not, put both
   strings on the same line
|  simplify_string_literals
:  bool
:  false
:  Whether to automatically convert multiline string literals without newlines,
   as well as f-strings without formatting into single-quoted string literals.
|  indent_before_comments
:  str
:  ' '
:  The indent to put before trailing comments
|  sticky_parens
:  bool
:  false
:  Controls whether parenthesis used for grouping are stuck to what they enclose
   or are exploded out to be on their own lines.
|  continuation_indent
:  bool
:  false
:  Controls whether a multiline conditional in an if statement gets an extra
   indent.


## install
	*muon* *install* [*-n*]

	Installs the project. The _DESTDIR_ environment variable is respected
	and will prefix all installation directories if it is present.

	*OPTIONS*:
	- *-n* - dry run
	- *-d* <destdir> - set destdir
	- *-U* - Uninstall files instead of installing them.  This works based on a
	  record of what was installed during a previous *muon* *install* run

## internal
	*muon* *internal* <command> [<args>]

	Internal contains several subcommands used by *muon* internally.  These
	commands are subject to change at any time and should not be relied
	upon.  Not all internal commands are documented.

	*COMMANDS*:
	- *eval* - evaluate a _source file_
	- *exe* - execute a command
	- *repl* - start a _meson dsl_ repl
	- *dump_funcs* - output all supported functions and arguments

## internal check
	*muon* *internal* *check* [*-p*|*-d*] [*-m* <mode>] <filename>

	Check if _filename_ parses.

	*OPTIONS*:
	- *-p* - print the parsed ast
	- *-d* - print the dissasembly
	- *-m* <mode> - set the compilation mode
	- *-f* - parse in formatting mode

## internal eval
	*muon* *internal* *eval* [*-e*] [*-s*] *-c* <command> | <filename> [<args>]

	Evaluate a _source file_.  The interpreter environment is
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
	- *-c* <command> - evaluate the string _command_ rather than reading from a
	  file

## internal exe
	*muon* *internal* *exe* [*-f* <input file>] [*-c* <output file>] [*-e*
	<env.dat>] [*-a* <args.dat>] [*-R* <file>] [-- <cmd> [<args>]]

	Execute <cmd> with arguments <args>.

	*OPTIONS*:
	- *-f* <input file> - pass _input file_ as stdin to <cmd>
	- *-c* <output file> - capture stdout of <cmd> and write it to _output
	  file_
	- *-e* <env.dat> - read and set environment variables from _env.dat_
	- *-a* <args.dat> - read and set command from _args.dat_
	- *-R* <file> - remove _file_ if it exists before executing the command

## internal repl
	*muon* *internal* *repl*

	Start a _meson dsl_ repl.  The functions available are limited as with
	*internal eval*.

## internal summary
	*muon* *internal* *summary*

	Print a previously configured project's summary.

## internal dump_funcs
	*muon* *internal* *dump_funcs*

	Print all supported functions, methods, and module functions with their
	arguments, argument types, and return types to stdout.  This subcommand
	is mainly useful for generating https://muon.build/status.html.

## meson
	\[*muon*\] *meson* ...

	A compatibility layer that attempts to translate all flags and
	operands from meson cli syntax to muon cli syntax.  For example, the
	following two commands:

	```
	muon meson setup build --werror --prefix=/
	muon meson test -C build --list
	```

	Would be translated into the following two muon versions respectively:

	```
	muon setup -Dwerror=true -Dprefix=/ build
	muon -C build test -l
	```

	This compatibility layer is also enabled when muon's executable is named
	_meson_.

	For a more detailed usage information you can use the following two
	commands:

	```
	muon meson -h
	muon meson <subcommand> -h
	```

	NOTE: This is a best-effort translation and does not guarantee or imply
	full cli compatibility.  Many unimplemented flags are ignored and
	attempting to use an unsupported subcommands will result in an error.

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
	*muon* *setup* [*-D*[subproject*:*]option*=*value...] [*-w*] [*-#*] <build
	dir|source dir>

	Interpret all _source files_ and generate _buildfiles_ in _build dir_.

	*OPTIONS*:
	- *-D* [subproject*:*]option*=*value - Set build options.  Options
	  are either built in or project-defined.  Subproject options can be
	  specified by prepending the subproject's name and a colon to the
	  *option*.  This option may be specified multiple times.
	- *-#* Display a progress bar during setup. Can be combined with muon -q to
	  only display a progress bar.
	- *-w* Clear all build caches before setup begins.

## subprojects
	*muon* *subprojects*

	Run various commands over all subprojects.  Only subprojects with .wrap
	files are supported.  Most commands accept a list of projects as trailing
	arguments.

	*OPTIONS*:
	- *-d* <directory> - manually specify subprojects directory.  By default
	  muon will evaluate the current project's project() call to determine the
	  value for subproject_dir.

	*COMMANDS*:
	- *check-wrap* - check if a wrap file is valid
	- *update* - update subprojects with .wrap files
	- *list* - list subprojects with .wrap files and their status
	- *clean* - clean wrap-git subprojects

## test
	*muon* *test* [*-a*] [*-b*] [*-d* <display mode>] [*-o* <output>] [*-e*
	<setup>] [*-f*] [*-j* <jobs>] [*-l*] [*-R*] [*-s* <suite>] [*-S*] [*-v
	\[*-v*]*] [<test> [<test>[...]]

	Execute tests defined in _source files_.

	The default is to execute all tests, but you can also specify which
	tests to execute on the command line.

	<test> should consist of an optional project name, followed by a colon
	and then a test name.  Either may be omitted. For example,

	"test name" and ":test name" will match all tests named 'test name' in
	any project.++
"proj:test name" will match all tests named 'test name' in the project 'proj'.++
"proj:" will match all tests in the project 'proj'.

	Additionally, the test name may be a glob expression.  For example,

	"proj:long\*" will match all tests with names starting with 'long' in
	the project 'proj'.

	*OPTIONS*:
	- *-a* - include all tests from all projects
	- *-b* - run benchmarks instead of tests
	- *-d* <display mode> - Control test progress output.  _display mode_ can
	  be one of *auto*, *dots*, or *bar*.  *dots* prints a '.' for success and
	  'E' for error, *bar* prints a progress bar with an error count.  The
	  default mode, *auto*, selects *bar* if the output device is a terminal or
	  *dots* otherwise.
	- *-o* <output mode> - Control test results output.  *term* prints failures
	  and output to the terminal, *html* generates a single-page html report,
	  and *json* outputs test information to a json file.
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
	- *-t* <factor> - multiply timeouts with _factor_. If _factor_ <= 0
	  test have infinite duration.
	- *-v* - Increase verbosity.  When passed once, print test results as
	  they are completed.  When passed twice, the stdout/stderr of tests is
	  not captured.

## version
	*muon* *version*

	Print out version information as well as enabled features.

# EXTENSIONS

*muon* provides some extra functions that may be called within _source files_.
They are documented below.

- *dbg()* - Begin an interactive debugger.
- *p(value)* - Print any value's internal representation.  For example,
  `p('hello')` prints `'hello'`.

# SEE ALSO

meson.build(5) meson-reference(3) meson(1)

# AUTHORS

Maintained by Stone Tickle <lattis@mochiro.moe>, who is assisted by other open
source contributors.  For more information about muon development, see
<https://sr.ht/~lattis/muon>.
