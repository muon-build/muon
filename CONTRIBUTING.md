# Contributions

Hello, thanks for considering contributing to muon.  Please send patches and
questions to [~lattis/muon@lists.sr.ht](mailto:~lattis/muon@lists.sr.ht).
Before making any big changes, please send a proposal to the mailing list so I
can give you some pointers, and make sure you don't waste your time.

# Style

Muon uses a style similar to the linux kernel.  A few differences are:

- the return type of a function goes on the line above its declaration.
  e.g.
  ```c
  int
  main(void)
  {
          return 0;
  }
  ```
- it still goes on the same line in a function prototype
  e.g.
  ```c
  int main(void);
  ```
- never omit braces for single statement if, else, for, while,  etc.
- avoid function-like macros except in exceptional cases
- it is OK (but not great) if your line is more than 80 characters
- please use fixed-width integer types (e.g. `uint32_t`, `uint64_t`, `uint8_t`)
  whenever possible

In general, just try to follow the style of surrounding code.

# Internals

## Error handling

All errors that can be checked, should be checked.  If an error is detected, an
error message should be printed using `interp_error`, or if there is no source
code associated with the error, `LOG_E`.  The error should be immediately
returned.  Most functions returning  a `bool` return `false` on error.  The most
common other type of error returning function has the return type
`enum iteration_result`.  These functions should return `ir_err` on error.

## Meson functions

All meson functions are defined in `functions` with a separate file for each
object type on which the function is defined.  Functions not defined on an
object are in `default.c`.  If the function implementation is sufficiently large,
it may be broken up into a separate file under `functions/<object
type>/<function name>.c`.

When declaring a new function, you need to add it to the "impl\_tbl" at the
bottom of the file.  All functions should call `interp_args()` before they do
anything, even if they don't take any arguments.  `interp_args` takes 3 arrays,
conventionally named `an` (args, normal), `ao` (args, optional), and `akw`
(args, keyword).  Any of these may be NULL.  In particular, if they are all NULL
then the function takes no arguments, and `interp_args` will ensure this is the
case.

Arguments that are always supposed to be a specific type should be marked as
such in the argument array, otherwise you should use the type `obj_any`.  In
addition to the object types in the `enum object_type` definition, two
additional object types are recognized by `interp_args`: `ARG_TYPE_NULL` and
`ARG_TYPE_GLOB`.

`ARG_TYPE_NULL` is only used as a sentinel, all the argument arrays *must* be
terminated with an element of type `ARG_TYPE_NULL`.  `ARG_TYPE_GLOB` can only be
used as the last element of `an`, and will store the remaining arguments in an
`obj_array`.  This is similar to e.g. `def func(a, b, *c):` in python, where `c`
is the "glob" argument.

You may also bitwise or any obj\_ type with `ARG_TYPE_ARRAY_OF`.  This "type"
will cause `interp_args` to do the following things:
  1. coerce single elements to arrays
    - `'hello' #=> ['hello']`
  2. flatten arrays
    - `['hello', [], [['world']]] #=> ['hello', 'world']`
  3. typecheck all elements of the array
    - given the "type" `ARG_TYPE_ARRAY_OF | obj_string`, the above examples
      would pass, but `['a', false]` would not.

## Workspace

The workspace is a structure that contains all the data for an entire build
setup, including all subprojects, AST, options, objects, strings.  Most
interpreter related functions take a workspace as one of their arguments.

## Objects, strings, and `uint32_t`

All objects and strings are stored and passed by id, rather than pointer.  Until
recently, all these ids were `uint32_t`s.  Since that hurts readability, and it
is somewhat inconvenient to type, `obj` and `str` (both `uint32_t` typedefs)
were introduced.  While these don't help with C type checking unfortunately,
they do help document the code, and are also convenient to type.  Internally,
these ids are tagged and a runtime assert should catch any mix-ups.

Currently, the migration from `uint32_t` is in progress, so be aware that some
code still refers to strings and objects that way.

## Objects

Meson objects are created with the `make_obj` function.  Currently all object
data (except for strings) is stored directly in the object in a union.  See
`object.h` for more information.  Conventionally, all objects are referred to by
id rather than a pointer.

## Workspace strings (wk\_str)

Workspace strings are handled similarly to objects.  They are created via the
`wk_str_push*` family of functions, and are generally referred to by id.  Their
memory is managed by the workspace, and with the current implementation, pushing
new strings can invalidate all string pointers.  This means you generally
shouldn't hold on to a pointer to a wk\_str for very long.  I hope to improve
this situation in the future, but for the time being you can uncomment the else
branch in `darr_get_mem` (`data/darr.c`) and run muon under valgrind to check for
errors.

## Memory

You may be wondering, what about resource cleanup? Garbage collection?  Muon's
current approach is to cleanup only once at the very end.  Why? Meson is not a
Turing-complete language, so we don't need to worry about long running programs.
Furthermore, comparing `meson setup` to `muon setup` on this project tells us
that even with this naive approach, we are still doing far better than `meson`.

meson setup build:
total heap usage: 48,266 allocs, 47,764 frees, 57,225,893 bytes allocated

muon setup build:
total heap usage: 405 allocs, 260 frees, 934,135 bytes allocated
