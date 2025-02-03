# Rationale

[Meson doesn't support user defined
functions](https://mesonbuild.com/FAQ.html#why-doesnt-meson-have-user-defined-functionsmacros)
as one of its core principles.  Why would muon relax this limitation?  Partially
because I believe that careful implementation of user defined functionality
would not be harmful. We have seen meson scripts can get
[plenty](https://github.com/xorvoid/meson-brainfuck)
[complex](https://github.com/annacrombie/meson-raytracer) anyways.  The main
reason however, is that requiring every last bit of functionality to be
implemented in the build system itself is fundamentally untenable, especially
if the build system will support multiple implementations.

- Many meson modules are effectively reusable chunks of meson code (calls to
  dependency, custom\_target, etc.), so it is not necessary to access things
  that a native module has access to like the interpreter state.
- Lowering the bar for module contributions (this is important because they
  often involve specific tools I have no experience with/interest in).
- Potential to some day share module code with other meson implementations.

That last point is a bit pie in the sky, but at least this makes it technically
possible from muon's point of view and proves that it has value.
