# Differences between muon and Meson

This document describes functional differences between muon and Meson.  None of
these is a set-in-stone design decision, just a reflection of the current state
of affairs.

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

muon will error on invalid substitutions in all cases, and recognizes a double
`@` (`@@`) as an escaped `@` in `string.format()` and `custom_target` command
arguments, analogous to `%%` in `printf`-like format strings.  This is currently
relied-upon internally, but is subject to change.

## `custom_target` replaces backslashes with slashes in the command arguments

In Meson, all backslashes in `custom_target` command line arguments are blindly
replaced to forward slashes.  This behavior is not present in muon.

Reference: https://github.com/mesonbuild/meson/issues/1564
