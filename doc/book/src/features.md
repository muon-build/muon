# Features

In addition to trying maintain Meson compatibility, Muon also has a few features
of its own.

### muon analyze

muon has a full-featured static analyzer for meson build scripts.  It can check
for undefined variables, unused variables, variable reassignment to different
types, undefined functions, undefined kwargs, missing required args, methods,
operator typechecking, and more.

See `muon analyze -h` for more information.

### muon fmt

A formatter for meson build scripts.

See `muon fmt -h` for more information.

### Debugger (WIP)

Muon has an interactive gdb-like debugger.  You can trigger it by setting
breakpoints with the -b flag on either setup or internal eval subcommands.
Inside the debugger you can look around, evaluate expressions, and step through
code.

### Built-In Ninja (samurai)

Muon has an embedded copy of [samurai](https://github.com/michaelforney/samurai)
(at `src/external/samurai`) which has been ported to use muon's cross-platform
layer and so works on all the platforms muon supports.  This means you can just
grab a single muon binary and that's all you need to build your project.

### VM

Muon's language runtime is a fully featured stack-based VM.  All scripts are
compiled to bytecode before execution.  This runtime is powerful enough to host
modules as well as basic cross-platform scripting.  See
[Scripting](scripting.md) for more information

### Fast

Muon is orders of magnitude faster than meson when your scripts are interpreter-
bound. This means that as long as muon doesn't have to shell out to the compiler
it is very fast.

Typically the most speedup can be seen during re-configs where most compiler
checks have been cached.
