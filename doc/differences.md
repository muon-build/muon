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
not.  Muon will accept `\` as an escape character in `string.format()` and
`custom_target`, but this is incompatible with meson.  `\` use in
`custom_target` command arguments is already incompatible though (see next
point).

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

## default option values

A number of builtin options have different default values:

- `default_library` is `'static'`

## native: true

In muon `native: true` always means "for the build machine", while in meson
sometimes it doesn't.

Meson distinguishes between cross and native builds.  A native build is one
where the build machine and the host machine are the same, while a cross build
is one where they differ.  Since this is a build system, "machine" in this
context is mostly synonymous with "toolchain for a machine".

A nice cross compilation feature that meson supports is the ability to toggle
which toolchain a particular target uses at configure time.  This is
accomplished by using the `native` keyword available for many functions. Passing
`native: false` causes the target/dependency to be created/looked up for the
host machine.  Likewise, `native: true` causes the target/dependency to be
created/looked up for the build machine.  In most cases omitting the native
keyword is the same as passing `native: false`, although some functions treat
the omission of `native` specially, such as `add_languages` adding languages for
both the host and build machine if the keyword is omitted.

The above paragraph is true for muon too, but it is actually subtly wrong for
meson.  You might be wondering, why meson uses `native: true/false` and not
something more readable such as `for_machine: 'build'/'host'`.  The reason is
that during a *native build*, meson effectively [ignores the native keyword].
While this makes sense on some level since the machines you are selecting
between are the same, it confuses the usage of the `native` keyword even more
than just the odd mapping from boolean to machine, and makes it easier to create
a build file that is broken for cross compilation.

For example (taken from [meson's own tests]), the following code is perfectly
valid during a native build:

```meson
meson.override_dependency('expat', declare_dependency())
dependency('expat')
dependency('expat', native : true)
dependency('expat', native : false)
```

But will break with a dependency not found error during a cross build.

[ignores the native keyword]: https://github.com/mesonbuild/meson/pull/8582#issue-841311146
[meson's own tests]: https://github.com/mesonbuild/meson/blob/6b99eeb2c99d4af4be2562b25507541bfd842692/test%20cases/common/240%20dependency%20native%20host%20%3D%3D%20build/meson.build#L15
