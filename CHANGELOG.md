TBD
---

 - Separator between directories and command changed to single hyphen
   as GNU libc getopt() eats the double hyphen.


1.1 (2018-11-11)
----------------

 - Greatly enhanced portability (specifically to Linux) by switching from
   kqueue to select.
 - Now returns a nonzero status if any job fails.


1.0 (2018-11-07)
----------------

Initial release.
