# Function declaration

Function definition is started with the `func` keyword optionally followed by an
identifier, and a list of arguments enclosed in parenthesis.  If the function
returns a value it must specify the type of that value by adding `-> <type>`
after the argument list.

- The identifier is _required_ if the function definition is a statement.
- The identifier is _not allowed_ if the function definition is in an expression.

Positional and keyword arguments are supported in the argument list.  Positional
arguments must come first.  Keyword arguments are specified by adding a colon
(:) to the end of the argument followed by an optional default value. All
arguments must specify their type by following the argument name with a type
specifier.

Function definition is ended with the `endfunc` keyword.

Examples:

```meson
func a() -> int
    return 1
endfunc

func b(arg1 int) -> int
    return arg1
endfunc

func c(arg1 int, kw str:, kw_with_default str: 'default')
    message(arg1)
    message(kw)
    message(kw_with_default)
endfunc

d = func()
    message('hello from an anonymous function!')
endfunc
```
