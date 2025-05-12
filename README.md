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

## [Features]

- `muon analyze` - a static analyzer and language server for meson.build files.
- `muon fmt` - a meson.build code formatter
- An interactive stepping debugger
- A built-in cross platform [ninja implementation]
- [fast]

## Status

`muon` is close to feature-complete with the core of meson for `c` and `c++`.

See the [Muon Reference] for more detailed information.

Things missing include:

- build optimizations like unity
- some `b_` options
- dependencies with a custom configuration tool
- some modules

Other differences from meson are described in the [Muon Docs].

If you want to contribute, try using `muon` to build your favorite project.
Patches and bug reports welcome!

Additionally, muon is not fully supported on all platforms yet.  The current
status may be viewed on muon's [ci dashboard].  Platforms with some or all tests
disabled are currently WIP and platforms not tested in CI have unknown status.
In general, posix systems should work fine.  Windows support is improving
slowly.

## Dependencies

Essential:

- A c99 compatible toolchain

For `pkgconfig` support:

- A `pkg-config` binary
- Or `libpkgconf` (`-Dlibpkgconf=enabled`)

For `[wrap-file]` support:

- `libcurl`
- `libarchive`

To build documentation:

- `scdoc` for muon.1 and meson.build.5
- `python3` and `py3-yaml` for docs imported from the meson project

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

This will by default build a ninja implementation (samu) into the resulting
executable.  To disable this behavior use `CFLAGS=-DBOOTSTRAP_NO_SAMU`.

Stage 2:

```
build/muon-bootstrap setup build
build/muon-bootstrap -C build samu
build/muon-bootstrap -C build test
build/muon-bootstrap -C build install
```

## Contribute

Please refer to the [contributing guide] before sending patches.  Send patches
on the [mailing list], report issues on the [issue tracker], and discuss in
[#muon on libera.chat].

## License

`muon` is generally licensed under the GPL version 3.  For a more detailed
breakdown, each file contains license and copyright information at the top.  All
referenced licenses can be found in the LICENSES directory.

## Credits

Although I had already had the idea to re-implement meson in C, I was initially
inspired to actually go out and do it when I saw [boson].  `muon`'s code was
originally based on `boson`, though has since been almost completely rewritten.

[muon]: https://muon.build
[contributing guide]: https://docs.muon.build/contributing.html
[mailing list]: https://lists.sr.ht/~lattis/muon/
[issue tracker]: https://todo.sr.ht/~lattis/muon/
[#muon on libera.chat]: ircs://irc.libera.chat/#muon
[meson project tests]: https://github.com/mesonbuild/meson/tree/master/test%20cases
[Apache 2.0]: https://www.apache.org/licenses/LICENSE-2.0.txt
[boson]: https://sr.ht/~bl4ckb0ne/boson/
[Fast]: https://github.com/annacrombie/meson-raytracer#performance
[ninja implementation]: https://git.sr.ht/~lattis/muon/tree/master/item/src/external/samurai/README.md
[ci dashboard]: https://muon.build/muon_ci.html
[Features]: https://docs.muon.build/features.html
[Muon Docs]: https://docs.muon.build/differences.html
[Muon Reference]: https://docs.muon.build/reference
