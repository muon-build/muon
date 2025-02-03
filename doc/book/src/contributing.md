# Contributing

Thanks for considering contributing to muon.  Please send patches and questions
to <~lattis/muon@lists.sr.ht>.  Alternatively you may make a pull request at
<https://github.com/muon-build/muon>. Before making any big changes, please send
a proposal to the mailing list or open an issue so I can give you some pointers,
and make sure you don't waste your time.

## Contribution areas

There are two main ways to contribute to Muon:
- Add a [script module](scripting/adding_new_builtin.md).  There are several
  unimplemented modules that could likely be easily written as a script module.
  See `src/script/modules` for examples.
- Work on the runtime.  Please read the documentation on [Muon
  Internals](internals.md).

## C Style

Muon uses a style similar to the linux kernel.  A few differences are:

- the return type of a function goes on the line above its declaration.
  e.g.

  ```c
  int
  main(void)
  {
          return 0;
  }
  ```

- it still goes on the same line in a function prototype
  e.g.

  ```c
  int main(void);
  ```

- never omit braces for single statement if, else, for, while,  etc.
- avoid function-like macros except in exceptional cases
- it is OK (but not great) if your line is more than 80 characters
- please use fixed-width integer types (e.g. `uint32_t`, `uint64_t`, `uint8_t`)
  whenever possible

As usual, just try to follow the style of surrounding code.
