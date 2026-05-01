# Scope

Modules have their own scope and cannot access any external variables (save
builtins such as meson, build\_machine, etc.).

Functions also get their own scope, but can capture variables that have been
declared in parent scopes prior to the function definition.

Variable declaration implicitly occurs the first time it is assigned.  To force
a local declaration (e.g. to shadow a variable) you may use the `:=` syntax.

For examples see `tests/lang/scope.meson`.
