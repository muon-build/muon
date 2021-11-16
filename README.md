# muon

`muon` is an implementation of the meson build system in C with minimal
dependencies.

## Non-features

- bug-for-bug compatibility with meson.  In fact, `muon` aspires to be stricter
  than meson in cases where meson's implementation seems error prone.  `muon`
  uses the official meson documentation as its specification.
- cli compatibility with meson.  `muon` has different flags, subcommands, etc.,
  and should _not_ be renamed/symlinked to meson.

Contributions welcome:
- language support for languages other than C and C++

## Status

`muon` is complete enough to build complicated projects, however, many things
are still not implemented.  If you want to contribute, try using `muon` to build
your favorite project.  Patches and bug reports welcome!

## Requirements

`muon` requires various POSIX interfaces and a compiler offering c11 support.

In addition, dependency discovery requires `libpkgconf`, and wrap support
requires `libcurl` and `libarchive`.

A ninja-compatible build tool (e.g.
[samurai](https://github.com/michaelforney/samurai)) is also required to
bootstrap muon, but can be optionally embedded into muon after bootstrapping.

## Building

You can bootstrap muon like this:

```sh
./bootstrap.sh build
```

You can then use the bootstrapped muon to build itself:

```
build/muon setup build
ninja -C build
```

## Contributing

Please refer to the contributing
[guide](https://git.sr.ht/~lattis/muon/tree/master/item/CONTRIBUTING.md) before
sending patches.

## License

`muon` is licensed under the GPL version 3 (see LICENSE).  Tests under
`tests/project` were copied from the [meson
project](https://github.com/mesonbuild/meson/tree/master/test%20cases) and are
licensed under [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0.txt).

## Credits

Although I had already had the idea to re-implement meson in C, I was initially
inspired to actually go out and do it when I saw
[boson](https://sr.ht/~bl4ckb0ne/boson/).  `muon`'s code was originally based on
`boson`, though has since been almost completely rewritten.
