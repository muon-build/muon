<!--
SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
SPDX-License-Identifier: GPL-3.0-only
-->

# muon - features

In addition to trying maintain meson compatibility, muon also has a few features
of its own.

### muon analyze

muon has a full-featured static analyzer for meson build scripts.  It can check
for undefined variables, unused variables, variable reassignment to different
types, undefined functions, undefined kwargs, missing required args, methods,
operator typechecking, and more.

### muon fmt

A formatter for meson build scripts.

### debugger

muon has an interactive gdb-like debugger.  You can trigger it by setting
breakpoints with the -b flag on either setup or internal eval subcommands.
Inside the debugger you can look around, evaluate expressions, and step through
code.

### built-in ninja

muon has an embedded copy of samurai (at src/external/samurai) which has been
ported to use muon's cross-platform layer and so works on all the platforms muon
supports.  This means you can just grab a single muon binary and that's all you
need to build your project.

### fast

muon is orders of magnitude faster than meson when your scripts are interpreter-
bound. This means that as long as muon doesn't have to shell out to the compiler
it is very fast.
