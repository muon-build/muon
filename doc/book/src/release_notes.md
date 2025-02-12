# Release Notes

## 0.4.0

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

## 0.3.0

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
