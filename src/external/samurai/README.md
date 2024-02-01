This is a port of [samurai] by Michael Forney.

Most of the changes are to make use of muon's platform layer.  This makes
samurai cross-platform, and also allows it to be more easily executed in the
same process as muon itself by ensuring all resources are cleaned up and not
only relying on program exit.

Other changes include refactoring all allocations to use an arena allocator,
removing all global mutable variables, removing various assumptions like
renaming an open file, and other various fixes.

[samurai]: https://github.com/michaelforney/samurai
