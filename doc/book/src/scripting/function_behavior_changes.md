# Function behavior changes

This document lists all modifications to function behavior when a function is
called during scripting mode vs external mode.

- In regular meson functions which create targets, the `output` parameter is not
  allowed to contain file separators. This has the effect that targets must only
  produce outputs which live in the current build directory. In extended meson
  file separators are permitted in these cases and so extended meson functions
  can create targets whose outputs may exist in other build directories.
