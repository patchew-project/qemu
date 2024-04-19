----------------
QAPI Domain Test
----------------

.. qapi:module:: foo-module
   :no-index:

   This starts a hypothetical module named ``foo-module``, but it
   doesn't create a cross-reference target and it isn't added to the
   index.

   Check out the `genindex` or the `qapi-index` for proof that
   foo-module is not present.

.. qapi:module:: bar-module
   :no-typesetting:

   This starts a hypothetical module named ``bar-module``, but the
   contents of the body here will not be rendered in the
   output. However, any link targets created here or in nested
   directives will be preserved and functional.

   Check out the `genindex` for proof that bar-module is present in two
   places! (under both "bar-module" and "QAPI module".)

.. qapi:module:: block-core

   Block core (VM unrelated)
   =========================

   This starts the documentation section for the ``block-core`` module.
   All documentation objects that follow belong to the block-core module
   until another ``qapi:module:`` directive is encountered.

   This directive does not create an entry in the sidebar or the TOC
   *unless* you create a nested section title within the directive.

   The ``block-core`` module will have two entries in the `genindex`,
   under both "block-core" and "QAPI module".

   Modules will also be reported in the `qapi-index`, under the Modules
   category and in the alphabetical categories that follow.


QAPI modules can now be cross-referenced using the ```any```
cross-referencing syntax. Here's a link to `bar-module`, even though
the actual output of that directive was suppressed. Here's a link to
`block-core`. A link to ```foo-module``` won't resolve because of the
``:no-index:`` option we used for that directive.

Explicit cross-referencing syntax for QAPI modules is available with
``:qapi:mod:`foo```, here's a link to :qapi:mod:`bar-module` and one to
:qapi:mod:`block-core`.


.. qapi:command:: example-command

   This directive creates a QAPI command named `example-command` that
   appears in both the `genindex` and the `qapi-index`. As of this
   commit, there aren't any special arguments or options you can give to
   this directive, it merely parses its content block and handles the
   TOC/index/xref book-keeping.

   Unlike the QAPI module directive, this directive *does* add a TOC
   entry by default.

   This object can be referenced in *quite a few ways*:

   * ```example-command``` => `example-command`
   * ```block-core.example-command``` => `block-core.example-command`
   * ``:qapi:cmd:`example-command``` => :qapi:cmd:`example-command`
   * ``:qapi:cmd:`block-core.example-command``` => :qapi:cmd:`block-core.example-command`
   * ``:qapi:cmd:`~example-command``` => :qapi:cmd:`~example-command`
   * ``:qapi:cmd:`~block-core.example-command``` => :qapi:cmd:`~block-core.example-command`
   * ``:qapi:obj:`example-command``` => :qapi:obj:`example-command`
   * ``:qapi:obj:`block-core.example-command``` => :qapi:obj:`block-core.example-command`
   * ``:qapi:obj:`~example-command``` => :qapi:obj:`~example-command`
   * ``:qapi:obj:`~block-core.example-command``` => :qapi:obj:`~block-core.example-command`

   As of Sphinx v7.2.6, there are a few sphinx-standard options this
   directive has:

   * ``:no-index:`` or ``:noindex:`` Don't add to the `genindex` nor
     the `qapi-index`; do not register for cross-references.
   * ``:no-index-entry:`` or ``:noindexentry:``
   * ``:no-contents-entry:`` or ``:nocontentsentry:``
   * ``:no-typesetting:``
