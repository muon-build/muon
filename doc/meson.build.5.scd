meson.build(5)

; SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
; SPDX-License-Identifier: GPL-3.0-only

# NAME

meson.build - a build system dsl

# DESCRIPTION

The meson dsl is a dynamically typed language, similar to other interpreted
languages like python and ruby.  All objects are immutable. Functions cannot be
defined, only built-in functions are available.

- *STRUCTURE* - overview of source and project layout
- *TYPES* - information on all primitive types
- *SYNTAX* - description of syntax and keywords

# STRUCTURE

All meson projects must have a file called `meson.build` at the project root.
This is the root build file.  This file must have a call to the `project()`
function as its first statement.  Additional `meson.build` files may reside in
any subdirectories of the project.  These build files are then executed with
the `subdir()` function.

# TYPES

The meson build system dsl contains most of the common types that would be
expected:

- booleans (written as `true` and `false`)
- integers
- strings
- arrays
- dictionaries

## Booleans

Booleans are either `true` or `false`.

*OPERATORS*
- `and` - logical and
- `or` - logical or
- `not` - logical and

## Integers

You can specify an integer literal using decimal, hexadecimal, octal, and
binary.

```
int_1 = 1
int_42 = 42
int_255 = 0xFF
int_493 = 0o755
int_1365 = 0b10101010101
```

All common arithmetic operations are implemented for integers: addition,
subtraction, division, multiplication, and modulo.

*OPERATORS*
- `\*` - multiplication
- `/` - integer division
- `%` - modulo (remainder)
- `+` - addition
- `-` - subtraction
- `==` - equal to
- `!=` - not equal to
- `<` - less than
- `>` - greater than
- `<=` - less than or equal to
- `>=` - greater than or equal to

## Strings

Strings in Meson are declared with single quotes.  `\` is the escape character.

The full list of escape sequences is:

- `\\` Backslash
- `\'` Single quote
- `\a` Bell
- `\b` Backspace
- `\f` Formfeed
- `\n` Newline
- `\r` Carriage Return
- `\t` Horizontal Tab
- `\v` Vertical Tab
- `\ooo` Character with octal value ooo, up to 3 digits long
- `\xhh` Character with hex value hh
- `\uxxxx` Character with 16-bit hex value xxxx
- `\Uxxxxxxxx` Character with 32-bit hex value xxxxxxxx
- `\N{name}` Character named name in Unicode database

Multi-line strings are surrounded by 3 consecutive quotes.  These are raw
strings that do not support the escape sequences listed above.

Format strings are expressed by a placing leading `f` before the first opening
quote.  Inside of a format string, sequences of the form `@[a-z_]+@` will be
substituted with the value of the matching variable.

```
name = 'Alice'

# prints "Hello Alice"
message(f'Hello @name@')
```

*OPERATORS*
- `+` - concatenate two strings
- `/` - concatenate two strings as if `join_paths()` was called
- `[n]` -  access the character at index `n`
- `==` - equal to
- `!=` - not equal to

## Arrays

Arrays are delimited by brackets. An array can contain an arbitrary number of objects of any type.

*OPERATORS*
- `+` -  If the rhs operand is an array, it will be joined to the lhs
  array. If it is a scalar, it will be appended to the lhs array.
- `[n]` - access the object at index `n`
- `<obj> in <array>` - check if object `obj` is in `array`
- `<obj> in <array>` - check if object `obj` is not in `array`
- `==` - equal to
- `!=` - not equal to

## Dictionaries

Dictionaries are delimited by curly braces. A dictionary can contain an
arbitrary number of key: value pairs. Keys are required to be strings, but
values can be objects of any type.  Dictionaries are immutable and do not have
a guaranteed order.

*OPERATORS*
- `+` - merge two dictionaries.  In case of a conflicting key, the value from
  the rhs dictionary will be taken.
- `[key]` - access the object with key `key`
- `<key> in <dict>` - check if key `key` is in `dict`
- `<key> not in <dict>` - check if key `key` is not in `dict`
- `==` - equal to
- `!=` - not equal to

# SYNTAX

A meson build file is composed of statements, which are terminated by newlines.
Other than the statement-terminating newline, white space has no syntactic
meaning.

## Comments

A comment starts with the `#` character and extends until the end of the line.

## Variables

A variable can contain a value of any type, and does not need to be
predeclared.

```
var1 = 'hello'
var2 = 102
```

One important difference in how variables work in the dsl is that all objects
are immutable. When you see an operation which appears like a mutation,
actually a new object is created and assigned to the name.

```
var1 = [1, 2, 3]
var2 = var1
var2 += [4]
# var2 is now [1, 2, 3, 4]
# var1 is still [1, 2, 3]
```

## Function and method calls

Builtin functions are called by their name followed by parenthesis containing
optional, comma-separated arguments.  Arguments are either positional or
keyword.  Keyword arguments are expressed using the keyword without quotes,
followed by a colon.

```
foo()
foo('bar')
foo('bar', baz: true)
```

Method calls are expressed by a `.` followed by the same function call syntax as above.

```
foo.bar()
foo.bar('baz', qux: false)
```

For a complete list of functions and methods, please see `meson-reference(3)`.

## If statements

Start an if statement with the `if` keyword followed by a boolean expression.
Further conditions can be expressed using the `elif` keyword followed by a
boolean expression.  The `else` keyword can be used to handle the case when no
conditions are matched.  An if statement is terminated with the `endif`
keyword.

```
if conditon1
elif condition2
else condition3
endif
```

## Foreach statements

To loop over values in an iterable, use the `foreach` keyword followed by a
comma separated list of names to assign the values of the iterable to, a colon,
and an iterable expression.  Only arrays and dictionaries are iterable.  A
foreach statement is terminated with the `endforeach` keyword.

Arrays have one value to assign to.

```
foreach value : array
	foo(value)
endforeach
```

Dictionaries have one two values to assign to.

```
foreach key, value : dictionary
	foo(key)
	bar(value)
endforeach
```

Inside a `foreach` block you may use the `break` and `continue` keywords.
\`break` exits the loop immediately.  `continue` skips the rest of the current
iteration.

# SEE ALSO

meson-reference(3) meson(1) muon(1)
