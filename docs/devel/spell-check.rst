===========
Spell Check
===========

QEMU uses British or American English in code and documentation. There
are some typical spelling bugs which can be easily avoided by using
tools like codespell.

codespell is a python script which detects (and optionally fixes) the
most common spelling bugs.

If you installed codespell in your HOME directory, it can be called from
the QEMU source directory like this::

    ~/bin/codespell.py -d -r -s -x scripts/codespell.exclude -q 2 ~/share/codespell/dictionary.txt

``-x scripts/codespell.exclude`` excludes some known lines from the check
and needs a file which is not yet committed.

.. _external_links:

External Links
--------------

-  http://packages.profusion.mobi/codespell/ (download)
-  http://git.profusion.mobi/cgit.cgi/lucas/codespell/ (git repository)
-  https://github.com/lucasdemarchi/codespell (alternate git repository)
-  http://en.wikipedia.org/wiki/Wikipedia:Lists_of_common_misspellings/For_machines
-  http://grammar.yourdictionary.com/spelling-and-word-lists/misspelled.html
