# Release Notes

##   0.7.0
- more cross file support
- add a way to disable docs
- set more muon meson defaults to improve drop in behavior
- fully implement meson.add_devenv
- improvements to rpath fixer
- improve haiku support
- bump meson support to 1.11
    - 1.8.0
        - New argument android_exe_type for executables *stub*
        - Changes to the b_sanitize option
        - i18n module xgettext *stub*
        - version_compare now accept multiple compare strings
        - ~Improvements to Objective-C and Objective-C++~
        - Per project subproject options rewrite*
            - Note: internals are done, but some questionable edge cases such as
              supporting -U and -A, or setting project options without yielding
              them using -D:foo=bar syntax are not implemented.
    - 1.9.0
        - Array .flatten() method
        - ~Support response files for custom targets~ (postponing for now,
              also unclear why this would be needed since custom_targets already
              have rsp-like behavior using `muon internal exe -a`)
        - Added license keyword to pkgconfig.generate
        - pkgconfig.generate supports internal dependencies in requires
    - 1.10.0
        - Support for the counted_by attribute
        - Added a values() method for dictionaries
        - Add cmd_array method to ExternalProgram
        - Added OS/2 support (shortname kwarg / os2_emxomf builtin opt) *stub*
        - Array .slice() method
        - -Db_vscrt on clang
        - Added build_subdir arg to various targets
        - Methods from compiler object now accept strings for include_directories (already supported)
        - Using meson.get_compiler() to get a language from another project is marked (already an error)
        - Add a configure log in meson-logs (already supported)
        - Added new namingscheme option
        - New method to handle GNU and Windows symbol visibility for C/C++/ObjC/ObjC++
    - 1.11.0
        - BuildTarget(install_dir) length > 1 replaced with keywords
        - Deprecate should_fail and rename it to expected_fail, also introduce expected_exitcode
        - install_man and install_headers: add support for install_tag kwarg
        - Added link_early_args to targets performing linking
        - Machine files now expand ~ as the user's home directory
        - windows.compile_resources now detects header changes with rc.exe
        - Added implicit_include_directories argument to windows.compile_resources
        - Console kwarg on run_command

## ✓ 0.6.0

- internals / language features
    - in script mode: refactor how scope works, all variable resolution happens
      at compile time, globals are disallowed.  Improves performance and
      clarifies scoping rules.
      (github pr)[https://github.com/muon-build/muon/pull/241]
    - all memory is tracked an managed with an arena allocator.  This should
      improve memory usage, performance, and developer ergonomics.
- toolchains
    - toolchains are now defined in scripts: `src/script/runtime/toolchains.meson`
    - tcc
    - clang-cl
    - ccache and sccache
- documentation
    - `muon help`
    - source links in [reference manual](https://docs.muon.build/reference)
- tooling
    - DAP debugger protocol support & dcmake integration
      [github issue](https://github.com/muon-build/muon/issues/249)
- wayland module
- support cross/native files
- 12 contributors:
    - Christopher Wellons (1)
    - Daniel Wagner (1)
    - Fredrik Foss-Indrehus (1)
    - kzc (20)
    - Michael Forney (3)
    - Sertonix (1)
    - Stone Tickle (544)
    - Teselka (9)
    - VaiTon (14)
    - Vincent Torri (2)
    - vtorri (1)
    - Zephyr Lykos (1)

## ✓ 0.5.0

- muon analyze now has an LSP mode
- pkg-config-exec backend added.  muon now supports shelling out to pkg-config
  (or pkgconf).
- bump meson support to 1.7
- more builtin dependency handlers
- lots of bug fixes
- 9 contributors:
    - Jonathan Schleifer (3)
    - LoneFox78 (1)
    - Michael Forney (1)
    - NRK (6)
    - Stone Tickle (416)
    - VaiTon (3)
    - Vincent Torri (2)
    - kzc (4)
    - m-hugo (1)

## ✓ 0.4.0

- More windows improvements
    - only ~30 tests failing with msvc
    - vsenv ported from meson
    - .exe deployed by CI
- Coverage targets are now supported thanks to Andrew McNulty
- Various default compiler options were brought in line with meson thanks to
  Michal Sieron
- XCode backend
- Improvements/bug fixes for script modules
- gnome module improvements thanks to sewn.
- dependency() overhaul, now more closely matches meson's implementation
    - [Custom dependency handlers](https://git.sr.ht/~lattis/muon/tree/master/item/src/script/runtime/dependencies.meson)
      can be defined in script mode.
- `docs/*.md` ported to mdbook and hosted at <https://docs.muon.build>
- A [reference manual](https://docs.muon.build/reference) containing all the
  functions, modules, objects, and methods that muon knows about is
  auto-generated on every build.
  - It also generates documentation for script modules using doc comments!
- An experimental UI
- As always, lots of bugs fixed!
- 12 contributors:
    - Andrew McNulty (1)
    - Arsen Arsenović (1)
    - Eli Schwartz (1)
    - Jonathan Schleifer (1)
    - Jürg Billeter (1)
    - Michael Forney (2)
    - Michal Sieron (4)
    - Stone Tickle (303)
    - Theo Paris (1)
    - Vincent Torri (3)
    - kzc (1)
    - sewn (4)

## ✓ 0.3.0

- Brand-new interpreter: <https://mochiro.moe/posts/10-muon-internals/>
- Lots of windows improvements
    - bootstrap.bat
    - Simple tests passing
- Lots of macOS improvements
    - All tests passing
    - Universal binary deployed by CI
- Script modules introduced: a way of writing muon modules with mostly normal
  meson code.
    - i18n module
    - gnome module (wip)
- Embedded cross-platform samurai implementation
- Lots of bugs fixed!
- 17 contributors:
    - Andrea Pappacoda (1)
    - Andrew McNulty (11)
    - Eli Schwartz (1)
    - Filipe Laíns (4)
    - JCWasmx86 (1)
    - Michael Forney (2)
    - Michal Sieron (2)
    - Seedo Paul (27)
    - Sertonix (3)
    - Stone Tickle (497)
    - Thomas Adam (1)
    - Tokunori Ikegami (1)
    - Vincent Torri (12)
    - kzc (2)
    - rofl0r (1)
    - sewn (1)
    - torque (1)
