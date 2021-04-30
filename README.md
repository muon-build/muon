# boson

`boson` is an implementation of the meson language in written in C11 focusing
on portability and simplicity.

## Status

`boson` is currently a work in progress. There's a lot to do, don't expect it
to be able to build your project.


[![builds.sr.ht status](https://builds.sr.ht/~bl4ckb0ne/boson.svg)][6]

## Requirements

`boson` requires various POSIX interfaces and a compiler offering c11 support.

## Building

You can either use the Makefile:

```
mkdir build
cd build
../configure
make
```

Or the meson file

```
meson build
ninja -C build
```

## Contributing

Pick an issue on our [todo list][1], and [send a patch][2] to
[~bl4ckb0ne/boson@lists.sr.ht][3]. Visit us on [#boson][4] on [OFTC][5] for any
questions regarding usage or development

[1]: https://todo.sr.ht/~bl4ckb0ne/boson
[2]: https://git-send-email.io
[3]: https://lists.sr.ht/~bl4ckb0ne/boson
[4]: https://webchat.oftc.net/?channels=boson
[5]: https://www.oftc.net
[6]: https://builds.sr.ht/~bl4ckb0ne/boson
