----------------
QAPI Domain Test
----------------

.. this sets the code-highlighting language to QMP for this *document*.
   I wonder if I can set a domain default...?

.. highlight:: QMP


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
   :since: 42.0

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

.. qapi:command:: fake-command
   :since: 13.37
   :deprecated:
   :unstable:
   :ifcond: CONFIG_LINUX

   This is a fake command, it's not real. It can't hurt you.

   :arg int foo: normal parameter documentation.
   :arg str bar: Another normal parameter description.
   :arg baz: Missing a type.
   :arg no-descr:
   :arg BitmapSyncMode discrim: How about branches in commands?

   .. qapi:branch:: discrim on-success

      :arg str foobar: This is an argument that belongs to a tagged union branch.
      :arg int? foobaz: This is another argument belonging to the same branch.

   .. qapi:branch:: discrim never

      :arg str barfoo: This is an argument that belongs to a *different* tagged union branch.
      :arg int64 zzxyz: And this is another argument belonging to that same branch.

   :feat hallucination: This command is a figment of your imagination.
   :feat deprecated: Although this command is fake, you should know that
      it's also deprecated. That's great news! Maybe it will go away and
      stop haunting you someday.
   :feat unstable: This command, as a figment of your imagination, is
      highly unstable and should not be relied upon.
   :error CommandNotFound: When you try to use this command, because it
      isn't real.
   :error GenericError: If the system decides it doesn't like the
      argument values. It's very temperamental.
   :return SomeTypeName: An esoteric collection of mystical nonsense to
      both confound and delight.
   :example: This isn't a "semantic" field, but it's been added to the
      allowed field names list. you can use whatever field names you'd
      like; but to prevent accidental typos, there is an allow list of
      "arbitrary" section names.

      You can nestle code-blocks in here, too, by using the ``::``
      syntax::

         -> { [ "bidirectional QMP example" ] }
         <- { [ "hello world!"] }

      Or use explicit ``.. code-block:: QMP`` syntax, but it must start
      on its own line with a blank line both before and after the
      directive to render correctly:

      .. code-block:: QMP

         -> "Hello friend!"

      Note that the QMP highlighter is merely garden-variety JSON, but
      with the addition of ``->``, ``<-`` and ``...`` symbols to help
      denote bidirectionality and elided segments. Eduardo Habkost and I
      wrote this lexer many moons ago to support the
      :doc:`/interop/bitmaps` documentation.
   :see also: This is also not a "semantic" field. The only limit is
      your imagination and what you can convince others to let you check
      into conf.py.

   Field lists can appear anywhere in the directive block, but any field
   list entries in the same list block that are recognized as special
   ("arg") will be reformatted and grouped accordingly for rendered
   output.

   At the moment, the order of grouped sections is based on the order in
   which each group was encountered. This example will render Arguments
   first, and then Features; but the order can be any that you choose.

.. qapi:enum:: BitmapSyncMode
   :since: 4.2

   An enumeration of possible behaviors for the synchronization of a
   bitmap when used for data copy operations.

   :value on-success: The bitmap is only synced when the operation is
      successful. This is the behavior always used for
      ``INCREMENTAL`` backups.
   :value never: The bitmap is never synchronized with the operation, and
      is treated solely as a read-only manifest of blocks to copy.
   :value always: The bitmap is always synchronized with the operation,
      regardless of whether or not the operation was successful.

.. qapi:alternate:: BlockDirtyBitmapOrStr
   :since: 4.1

   :choice str local: name of the bitmap, attached to the same node as
      target bitmap.
   :choice BlockDirtyBitmap external: bitmap with specified node

.. qapi:event:: BLOCK_JOB_COMPLETED
   :since: 1.1

   Emitted when a block job has completed.

   :memb JobType type: job type
   :memb str device: The job identifier. Originally the device name but
      other values are allowed since QEMU 2.7
   :memb int len: maximum progress value
   :memb int offset: current progress value. On success this is equal to
      len. On failure this is less than len
   :memb int speed: rate limit, bytes per second
   :memb str? error: error message. Only present on failure. This field
      contains a human-readable error message. There are no semantics
      other than that streaming has failed and clients should not try to
      interpret the error string

   Example::

     <- {
       "event": "BLOCK_JOB_COMPLETED",
       "data": {
         "type": "stream",
         "device": "virtio-disk0",
         "len": 10737418240,
         "offset": 10737418240,
         "speed": 0
       },
       "timestamp": {
         "seconds": 1267061043,
         "microseconds": 959568
       }
     }

.. qapi:struct:: BackupPerf
   :since: 6.0

   Optional parameters for backup.  These parameters don't affect
   functionality, but may significantly affect performance.

   :memb bool? use-copy-range: Use copy offloading.  Default false.
   :memb int? max-workers: Maximum number of parallel requests for the
      sustained background copying process.  Doesn't influence
      copy-before-write operations.  Default 64.
   :memb int64? max-chunk: Maximum request length for the sustained
     background copying process.  Doesn't influence copy-before-write
     operations.  0 means unlimited.  If max-chunk is non-zero then it
     should not be less than job cluster size which is calculated as
     maximum of target image cluster size and 64k.  Default 0.

.. qapi:union:: RbdEncryptionOptions
   :since: 6.1

   :memb RbdImageEncryptionFormat format: Encryption format.
   :memb RbdEncryptionOptions? parent: Parent image encryption options
      (for cloned images).  Can be left unspecified if this cloned image
      is encrypted using the same format and secret as its parent image
      (i.e. not explicitly formatted) or if its parent image is not
      encrypted.  (Since 8.0)

   .. qapi:branch:: format luks

      :memb str key-secret: ID of a QCryptoSecret object providing a
         passphrase for unlocking the encryption

   .. qapi:branch:: format luks2

      :memb str key-secret: ID of a QCryptoSecret object providing a
         passphrase for unlocking the encryption

   .. qapi:branch:: format luks-any

      :memb str key-secret: ID of a QCryptoSecret object providing a
         passphrase for unlocking the encryption
