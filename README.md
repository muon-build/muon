# muon

`muon` is an implementation of the meson build system in C with minimal
dependencies.

## Non-features

- bug-for-bug compatibility with meson.  In fact, `muon` aspires to be stricter
  than meson in cases where meson's implementation seems error prone.  `muon`
  uses the official meson documentation as its specification.
- cli compatibility with meson.  `muon` has different flags, subcommands, etc.,
  and should _not_ be renamed/symlinked to meson.
  - additionally, various meson subcommands are not on the roadmap at all,
    including:
    - introspect
    - devenv
- phony build target generation (e.g. test, clean, etc.)

Contributions welcome:
- language support for languages other than C and C++
- dynamic library building

## Status

`muon` is complete enough to build complicated projects, however, many things
are still not implemented.  If you want to contribute, try using `muon` to build
your favorite project.  Bug reports welcome!

## Requirements

`muon` requires various POSIX interfaces and a compiler offering c11 support.

Dependency discovery requires `libpkgconf`.

Wrap support requires `libcurl` and `zlib`.

## Building

You can bootstrap muon like this:

```sh
./bootstrap.sh bootstrap
```

You can then use the bootstrapped muon to build itself:

```
bootstrap/muon setup build
ninja -C build
```

## Credits

Although I had already had the idea to re-implement meson in C, I was initially
inspired to actually go out and do it when I saw
[boson](https://sr.ht/~bl4ckb0ne/boson/).  `muon`'s code was originally based on
`boson`, though has since been almost completely rewritten.
