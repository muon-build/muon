# muon

`muon` is an implementation of the meson language in written in C11

## Status

`muon` is currently a work in progress. There's a lot to do, don't expect it to
be able to build your project.  In particular, `muon` only supports building C
projects.  If you'd like that to change, [send me an
email](mailto:lattis@mochiro.moe).

## Requirements

`muon` requires various POSIX interfaces and a compiler offering c11 support.

Depencency discovery requires `libpkgconf`.

Wrap support requires `libcurl` and `zlib`.

## Building

```
meson build
ninja -C build
```

## Credits

Although I had already had the idea to re-implement meson in C, I was initially
inspired to actually go out and do it when I saw
[boson](https://sr.ht/~bl4ckb0ne/boson/).  `muon`'s code was originally based on
`boson`, though has since been almost completely rewritten.
