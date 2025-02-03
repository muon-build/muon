# Scope

Scope generally works similar to other interpreted languages.

Modules have their own scope and cannot access any external variables (save
builtins such as meson, build\_machine, etc.).

Functions also get their own scope, but can see variables that have been
declared in parent scopes prior to the function definition.  Function
definitions also capture their scope. Re-assigning to a variable after it has
been captured will not affect the value of the captured variable.

Example:

```
func a() -> func
    i = 0
    func b() -> int
        i += 1
        return i
    endfunc

    return b
endfunc

counter = a()
counter() #=> 1
counter() #=> 2
new_counter = a()
new_counter() #=> 1
new_counter() #=> 2
counter() #=> 3
```

