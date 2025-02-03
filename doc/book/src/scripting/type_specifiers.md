# Type Specifiers

Type specifiers consist of primitive types with optional subtypes.

Supported primitive types are listed in the [Muon
Reference](https://docs.muon.build/reference) under the `Objects` header.

A value that can take one of many types can be specified by writing multiple
types separated with a `|` such as `int|str`.

There are a few preset combinations of types:

- `any` - all types except `null`
- `exe` - `str|file|external_program|python_installation|build_tgt|custom_tgt`

`dict` and `list` must be followed by a sub-type enclosed in square
brackets, e.g. `dict[str|int]` or `list[any]`.

You can also wrap the entire type in `glob[<type>]` or `listify[<type>]` to get
the special argument handling detailed in doc/contributing.md (search for
`TYPE_TAG_{GLOB,LISTIFY}`).

Applying the `glob` tag to the type of a kwarg causes any unmatched keywords to
be stored in that kwarg as a dict.
