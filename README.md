# [muon]

`muon` is an implementation of the meson build system in c99 with minimal
dependencies.

## Non-features

- bug-for-bug compatibility with meson.  In fact, `muon` aspires to be stricter
  than meson in cases where meson's implementation seems error prone.  `muon`
  uses the official meson documentation as its specification.
- cli compatibility with meson.  `muon` has different flags, subcommands, etc.,
  and should _not_ be renamed/symlinked to meson.

Other differences from meson are described in `doc/differences.md`

## Features

- `muon analyze` - a static analyzer for meson.build files.  Capable of doing
  type inference, checking unused variables, undeclared variables, etc.
- `muon fmt` - a meson.build code formatter
- An interactive stepping debugger with the `dbg()` function.
- Fast

## Status

`muon` is close to feature-complete (bugs notwithstanding!) with the core of
meson for `c` and `c++`.

Things missing include:

- cross-compilation support
- build optimizations like pch and unity
- several `b_` options
- dependencies with a custom configuration tool
- all modules except for `fs` and `pkgconfig`. (a small `python` module shim is
  also available)

If you want to contribute, try using `muon` to build your favorite project.
Patches and bug reports welcome!

## Dependencies

Bootstrap:

- `c99`
- ninja-compatible build tool

Bootstrap with `libpkgconf` support:

- `libpkgconf`
- `pkgconf` or `pkg-config`
- `sh`

`[wrap-file]` support:

- `libcurl`
- `libarchive`

Documentation:

- `scdoc` for muon.1 and meson.build.5
- `python3`
- `py3-yaml`

Tests:

- `python3`

## Building

The bootstrapping process has two stages.  The first stage produces a `muon`
binary capable of building itself (but not necessarily anything else). The
second stage produces the final binary.

Stage 1:

```
mkdir build
c99 -Iinclude src/amalgam.c -o build/muon
```

However, this version of muon will never be able to look up any dependencies.
If are going to need `dependency()` to work, use the provided bootstrapping
script, which links in `libpkgconf` if it is available.

```
./bootstrap.sh build
```

Stage 2:

```
build/muon setup build
ninja -C build
```

## Testing

```
build/muon -C build test
```

`muon` has a few of its own tests for core language features, but the majority
of the tests are copied from the meson project.

## Installation

```
build/muon -C build install
```

## Contributing

Please refer to the [contributing guide] before sending patches.  Send patches
on the [mailing list], report issues on the [issue tracker], and discuss in
[#muon on libera.chat].

## License

`muon` is licensed under the GPL version 3 (see LICENSE).  Tests under
`tests/project` were copied from the [meson project tests] and are licensed
under [Apache 2.0].

## Credits

Although I had already had the idea to re-implement meson in C, I was initially
inspired to actually go out and do it when I saw [boson].  `muon`'s code was
originally based on `boson`, though has since been almost completely rewritten.

[muon]: https://muon.build
[samurai]: https://github.com/michaelforney/samurai
[contributing guide]: https://git.sr.ht/~lattis/muon/tree/master/item/CONTRIBUTING.md
[mailing list]: https://lists.sr.ht/~lattis/muon/
[issue tracker]: https://todo.sr.ht/~lattis/muon/
[#muon on libera.chat]: ircs://irc.libera.chat/#muon
[meson project tests]: https://github.com/mesonbuild/meson/tree/master/test%20cases
[Apache 2.0]: https://www.apache.org/licenses/LICENSE-2.0.txt
[boson]: https://sr.ht/~bl4ckb0ne/boson/
