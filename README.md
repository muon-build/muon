<!--
SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
SPDX-FileCopyrightText: Andrea Pappacoda <andrea@pappacoda.it>
SPDX-License-Identifier: GPL-3.0-only
-->

<img src="https://muon.build/muon_logo.svg" alt="muon logo" height=64 />

# [muon]

muon is an implementation of the meson build system in c99 with minimal
dependencies.

## Features

- `muon analyze` - a static analyzer for meson.build files.  Capable of doing
  type inference, checking unused variables, undeclared variables, etc.
- `muon fmt` - a meson.build code formatter
- An interactive stepping debugger with the `dbg()` function.
- [Fast]

## Status

`muon` is close to feature-complete with the core of meson for `c` and `c++`.

See the [status page] for more detailed information.

Things missing include:

- cross-compilation support
- build optimizations like pch and unity
- some `b_` options
- dependencies with a custom configuration tool
- many modules

Other differences from meson are described in `doc/differences.md`

If you want to contribute, try using `muon` to build your favorite project.
Patches and bug reports welcome!

## Dependencies

Essential:

- `c99`
- a ninja-compatible build tool (`samu` can be optionally bootstrapped with
  `tools/bootstrap_ninja.sh`)

For `pkgconf` support:

- `libpkgconf`
- `pkgconf` or `pkg-config`

For `[wrap-file]` support:

- `libcurl`
- `libarchive`

To build documentation:

- `scdoc` for muon.1 and meson.build.5
- `python3` and `py3-yaml` for meson-reference.3

To run most project tests:

- `python3`

## Install

If you already have meson or muon and are not interested in bootstrapping, you
can just do a typical meson configure, build, install:

```
$meson setup build
cd build
ninja build
$meson test
$meson install
```

Otherwise, you must bootstrap muon.

The bootstrapping process has two stages.  The first stage produces a `muon`
binary capable of building itself (but not necessarily anything else). The
second stage produces the final binary.

Stage 1:

```
./bootstrap.sh build
```

Optionally, if your system does not provide a ninja-compatible build tool, you
may use the provided ninja bootstrapping script.

```
./tools/bootstrap_ninja.sh build
ninja=build/samu
```

Stage 2:

```
build/muon setup build
$ninja -C build
build/muon -C build test
build/muon -C build install
```

## Contribute

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
[status page]: https://muon.build/releases/edge/docs/status.html
[Fast]: https://github.com/annacrombie/meson-raytracer#performance
