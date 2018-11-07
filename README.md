within
======

Run a command in other directories:

**within** [**-j** *jobs*] *directories* **--** *command*


Description
-----------

Runs the given *command* in all given *directories*,
prepending directory names to output:

    $ within msort trickle -- make clean
    msort: rm -f *.o msort
    trickle: rm -f trickle tritty

When a single directory is given the **--** may be omitted:

    $ within code/msort git status
    code/msort: nothing to commit, working tree clean

The **-j** option specifies how many commands may be run simultaneously.
The default is 1.


Implementation
--------------

Based around a kqueue event loop. As many jobs are started as -j allows (1
by default), forking, descending into the given directory and executing the
command. For each job, both standard output and standard error are
redirected to a pipe that's read by a 'piper' which adds the 'directory:'
prefixes to the output. These pipers are effectively coroutines.

The event loop waits for finished jobs, starting new ones if there are
directories left, and for data on the pipes.


Running
-------

Requires an OS with kqueue, e.g. any BSD or macOS but not Linux or Windows.

Mac users can install from
[my Homebrew tap](https://github.com/sjmulder/homebrew-tap)

    brew install sjmulder/tap/within

To compile, install and uninstal from source:

    make
    make install   [DESTDIR=] [PREFIX=/usr/local] [MANPREFIX=PREFIX/man]
    make uninstall [DESTDIR=] [PREFIX=/usr/local] [MANPREFIX=PREFIX/man]


Legal
-----

Copyright (c) 2018, Sijmen J. Mulder (<ik@sjmulder.nl>)

within is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

within is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
more details.

You should have received a copy of the GNU Affero General Public License
along with within. If not, see <https://www.gnu.org/licenses/>.
