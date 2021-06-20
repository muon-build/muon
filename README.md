# muon

`muon` is an implementation of the meson build system in C with minimal
dependencies.

## Goals

`muon` aspires to generate build files functionally equivalent to what meson
generates.  Currently only a subset of meson's functionality is implemented, in
particular only C projects are supported.  Additionally, `muon` does not aspire
to be bug-for-bug compatible with meson, and will throw an error in some cases
where meson will not, but where it seems appropriate.

## Status

`muon` is complete enough to build complicated projects, but many unimplemented
corners remain.  Your best bet is to run it on your project, and submit a bug
report when you hit one.

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
