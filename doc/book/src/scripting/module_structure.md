# Module Structure

A script module consists of functions that perform the various module
operations. The final `return` at module scope is what "exports" the functions
to module consumers.  The returned object must be of type `dict[func]`.

```meson
func func1()
endfunc

func func2()
endfunc

return {
    'func1': func1,
    'func2': func2,
}
```

Using some of the new syntax for `dict` mutation we can define modules like this
too:

```meson
M = {}

M.func1 = func()
endfunc

M.func2 = func()
endfunc

return M
```
