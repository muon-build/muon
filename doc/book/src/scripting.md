# Scripting

Muon supports modules and small scripts written in meson.  In order to
facilitate this, various language features are enabled during module and script
evaluation.

## Evaluation Modes

Muon's implementation of the meson language includes three evaluation modes:

- `external`: Regular meson.build files external to muon's implementation are
  evaluated in this mode.
- `internal`: Any meson file evaluated using `muon internal eval ...` is
  evaluated in the internal mode.
- `extended`: A layering of internal on top of external, allows features from both
  modes to be used simultaneously.  This is the mode that script modules execute
  in.

Both `internal` and `extended` mode enable the same additional language features
collectively referred to as *scripting mode*. The primary difference between the
two modes lies in which functions may be called.  In particular, most functions
that require a project() or manipulate the build state in any way are not
available in `internal` mode.

## Scripting Mode

Muon's scripting mode consists of several langauge features as well as
additional functions and modules.

This guide focuses on the language features.  The additional functions and
modules are documented in the [Muon Reference](https://docs.muon.build/reference).  All
functions, modules, and arguments are tagged with their availability in muon's
different evaluation modes.
