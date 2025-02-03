# Adding a new builtin script module

After you have written your script module you must register it so it can be
imported.

All script modules live at `src/script/modules/<module_name>.meson`.  After
adding the module, you must modify `src/script/meson.build` to register it. muon
will embed the module source text in the built executable, and when the module
is import()ed that source will be interpreted.
