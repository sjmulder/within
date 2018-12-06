TBD
---
 - Fixed: wrong implicit permissions on installed files.

1.1.2 (2018-12-06)
------------------
 - Fixed: use-after-free of piper in list walk.
 - Fixed: EBADF caused by race condition.
 - Fixed: EMFILE caused by broken pipes left open.

1.1.1 (2018-11-12)
------------------
 - New: 'make check' target.
 - New: musl libc compatibility.
 - Fixed: double hyphen separator between directories and command was
   dropped by GNU getopt(). Separator changed to single hyphen instead.
 - Fixed: read errors were not reported.
 - Fixed: command output could go lost.

1.1 (2018-11-11)
----------------
 - Greatly enhanced portability (specifically to Linux) by switching from
   kqueue to select.
 - Now returns a nonzero status if any job fails.

1.0 (2018-11-07)
----------------
Initial release.
