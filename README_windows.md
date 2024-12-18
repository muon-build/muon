<!--
SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
SPDX-License-Identifier: GPL-3.0-only
-->

This document describes how to build [muon] for Windows using
bootstraping using different environments. Refer to README.md for
other informations.

# [MSYS2]

Once MSYS2 is installed, install also pkgconf, libcurl and libarchive,
untar one of the relase tarballs (or preferably clone muon from
sourcehut or github) and enter its directory. To build muon with
bootstrapping, execute:

```
CC="gcc -std=c99" ./bootstrap.sh build
CC="gcc -std=c99" ./build/muon-bootstrap.exe setup build
build/muon-bootstrap.exe -C build samu
build/muon-bootstrap.exe -C build install
```

muon provides an additional option (not available with meson) to set
the compiler, instead of overriding the `CC` environment variable:

```
./build/muon-bootstrap.exe setup -Denv.CC="gcc -std=c99" build
```

# Miscrosoft Visual Compiler (cl.exe)

Install Visual Studio Community 2022 and launch the native tools
command prompt for VS 2022 from the start menue. Then enter the muon
directory and run:

```
bootstrap.bat build
build\muon-bootstrap.exe setup build
build\muon-bootstrap.exe -C build samu
build\muon-bootstrap.exe -C build install
```

[muon]: https://muon.build
[MSYS2]: https://msys2.org/
