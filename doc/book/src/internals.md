# Muon Internals

## Error handling

All errors that can be checked, should be checked.  If an error is detected, an
error message should be printed using `vm_error`, or if there is no source
code associated with the error, `LOG_E`.  The error should be immediately
returned.  Most functions returning  a `bool` return `false` on error.  The most
common other type of error returning function has the return type
`enum iteration_result`.  These functions should return `ir_err` on error.

## Meson functions

All meson functions are defined in `functions` with a separate file for each
object type on which the function is defined.  Functions not defined on an
object are in `kernel.c`.  If the function implementation is sufficiently large,
it may be broken up into a separate file under `functions/<object
type>/<function name>.c`.

When declaring a new function, you need to add it to the "impl\_tbl" at the
bottom of the file.  All functions should call `pop_args()` before they do
anything, even if they don't take any arguments.  `pop_args` takes 3 arrays,
conventionally named `an` (args, normal), `ao` (args, optional), and `akw`
(args, keyword).  Any of these may be NULL.  In particular, if they are all NULL
then the function takes no arguments, and `pop_args` will ensure this is the
case.

Arguments should specify what types they accept by bitwise or-ing `tc_`-prefixed
types together.  `an` and `ao` argument arrays *must* be terminated by an
argument of type `ARG_TYPE_NULL`.

`TYPE_TAG_GLOB` can be or-d with the type of the last element of `an`, and will
store the remaining arguments in an `obj_array`.  This is similar to e.g. `def
func(a, b, *c):` in python, where `c` is the "glob" argument.

You may also bitwise or any type with `TYPE_TAG_LISTIFY`.  This "type" will
cause `pop_args` to do the following things:

1. coerce single elements to arrays
    - `'hello' #=> ['hello']`
2. flatten arrays
    - `['hello', [], [['world']]] #=> ['hello', 'world']`
3. typecheck all elements of the array
    - given the "type" `TYPE_TAG_LISTIFY | obj_string`, the above examples
      would pass, but `['a', false]` would not.

## Workspace

The workspace is a structure that contains all the data for an entire build
setup, including all subprojects, AST, options, objects, strings.  Most
interpreter related functions take a workspace as one of their arguments.

## Objects

Meson objects are created with the `make_obj` function.  See `object.h` for more
information.  Conventionally, all objects are referred to by id of type obj
rather than a pointer.

## Memory

You may be wondering, what about Garbage collection?  Muon's current approach is
to cleanup only once at the very end.  Meson is not a Turing-complete language,
so we don't need to worry about long running programs.
