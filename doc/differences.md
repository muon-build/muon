<!--
SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
SPDX-License-Identifier: GPL-3.0-only
-->

# Differences between muon and Meson

This document describes functional differences between muon and Meson.  None of
these is a set-in-stone design decision, just a reflection of the current state
of affairs.  This document is also not exhaustive, but is a best-effort list.

Some other small differences may be found by searching the tests/project for "#
different than meson" comments.

## nested subproject promotion

Meson performs nested subproject promotion.  This means that nested subprojects
become top-level subprojects, and all subprojects share the same namespace.

For example, given the following project structure:

```
.
├── meson.build
└── subprojects
    ├── a
    │   ├── meson.build
    │   └── subprojects
    │       └── b
    │           └── meson.build
    └── b
        └── meson.build
```

The order of `subproject` calls determines which subprojects will be used:

```meson
project('main')

# This causes all subprojects under subprojects/a/subprojects/ to be "promoted"
subproject('a')

# This will now use subprojects/a/subprojects/b, instead of subprojects/b
subproject('b')
```

muon does not perform subproject promotion.

## malformed escape sequences

Meson silently accepts malformed escape sequences and outputs them literally,
removing the leading escape character.  For example:

```meson
'\c'          # becomes 'c'
'\Uabcdefghi' # becomes 'Uabcdefghi'
'\xqr'        # becomes 'xqr'
```

In muon, malformed escape sequences are parse errors.

## format strings

Format strings in various parts of Meson use `@` as the delimiter.  The behavior
is inconsistent, `configure_file()` recognizes `\` as an escape character, but
format strings in `string.format()` and `custom_target` command arguments do
not.

`configure_file()`  will also warn you about invalid substitutions, and will
remove them in the output, `string.format()` will error on invalid
substitutions, and `custom_target` command arguments will be silently treated as
literals if they are invalid substitutions (e.g. `@BAZ@`).

Because some projects rely on the above custom target command argument
behaviour, muon merely adds a warning for invalid substitutions.  In all other
cases muon will raise error.

## `custom_target` replaces backslashes with slashes in the command arguments

In Meson, all backslashes in `custom_target` command line arguments are blindly
replaced to forward slashes.  This behavior is not present in muon.

Reference: <https://github.com/mesonbuild/meson/issues/1564>

## `build_target()` functions with empty sources

Meson allows you to create build targets (`executable()`, `shared_library()`,
`static_library()`, etc.) without specifying any sources.  In muon this is an
error.

## global compiler cache

Meson maintains a global compiler cache, which means that all languages added by
subprojects are available to the main project and vice-versa.  This can hide
bugs that will surface if the subproject is built by itself, or subproject calls
are rearranged.

## run\_command() cwd

Meson executes run\_command() commands in the current subdirectory, while muon
executes them in the project root.  Neither behaviour should be relied upon
however, since the docs say that it runs commands from an unspecified directory.

## backslash escaping in compiler defines

Meson replaces `\` with `\\` in compiler defines.  This is legacy behavior that
prevents you from using things like C escapes (e.g. `\n`) in compiler defines,
at the benefit of making it easier to use windows paths.  See meson commit
aca93df184a32ed7faf3636c0fbe90d05cb67857 for more information:

> Jon Turney:
> Now that all command-line escaping for ninja is dealt with in the ninja
> backend, escape_extra_args() shouldn't need to do anything.
>
> But tests of existing behaviour rely on all backslashes in defines being
> C escaped: This means that Windows-style paths including backslashes can
> be safely used, but makes it impossible to have a define containing a C
> escape.

## MESONINTROSPECT

Since muon does not offer an introspection subcommand, `MESONINTROSPECT` is not
set in the environment of run\_command, test, custom\_target, etc.  `MUON_PATH`
is provided for users who are waiting for
<https://github.com/mesonbuild/meson/pull/9855> and are (ab)using
`MESONINTROSPECT` for this purpose.
