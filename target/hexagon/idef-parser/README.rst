Hexagon ISA instruction definitions to tinycode generator compiler
------------------------------------------------------------------

idef-parser is a small compiler able to translate the Hexagon ISA description
language into tinycode generator code, that can be easily integrated into QEMU.

Compilation Example
-------------------

To better understand the scope of the idef-parser, we'll explore an applicative
example. Let's start by one of the simplest Hexagon instruction: the ``add``.

The ISA description language represents the ``add`` instruction as
follows:

.. code:: c

   A2_add(RdV, in RsV, in RtV) {
       { RdV=RsV+RtV;}
   }

idef-parser will compile the above code into the following code:

.. code:: c

   /* A2_add */
   void emit_A2_add(DisasContext *ctx, Insn *insn, Packet *pkt, TCGv_i32 RdV,
                    TCGv_i32 RsV, TCGv_i32 RtV)
   /*  { RdV=RsV+RtV;} */
   {
       tcg_gen_movi_i32(RdV, 0);
       TCGv_i32 tmp_0 = tcg_temp_new_i32();
       tcg_gen_add_i32(tmp_0, RsV, RtV);
       tcg_gen_mov_i32(RdV, tmp_0);
       tcg_temp_free_i32(tmp_0);
   }

The output of the compilation process will be a function, containing the
tinycode generator code, implementing the correct semantics. That function will
not access any global variable, because all the accessed data structures will be
passed explicitly as function parameters. Among the passed parameters we will
have TCGv (tinycode variables) representing the input and output registers of
the architecture, integers representing the immediates that come from the code,
and other data structures which hold information about the disassemblation
context (``DisasContext`` struct).

Let's begin by describing the input code. The ``add`` instruction is associated
with a unique identifier, in this case ``A2_add``, which allows to distinguish
variants of the same instruction, and expresses the class to which the
instruction belongs, in this case ``A2`` corresponds to the Hexagon
``ALU32/ALU`` instruction subclass.

After the instruction identifier, we have a series of parameters that represents
TCG variables that will be passed to the generated function. Parameters marked
with ``in`` are already initialized, while the others are output parameters.

We will leverage this information to infer several information:

-  Fill in the output function signature with the correct TCGv registers
-  Fill in the output function signature with the immediate integers
-  Keep track of which registers, among the declared one, have been
   initialized

Let's now observe the actual instruction description code, in this case:

.. code:: c

   { RdV=RsV+RtV;}

This code is composed by a subset of the C syntax, and is the result of the
application of some macro definitions contained in the ``macros.h`` file.

This file is used to reduce the complexity of the input language where complex
variants of similar constructs can be mapped to a unique primitive, so that the
idef-parser has to handle a lower number of computation primitives.

As you may notice, the description code modifies the registers which have been
declared by the declaration statements. In this case all the three registers
will be declared, ``RsV`` and ``RtV`` will also be read and ``RdV`` will be
written.

Now let's have a quick look at the generated code, line by line.

::

   tcg_gen_movi_i32(RdV, 0);

This code starts by zero-initializing ``RdV``, since reading from that register
without initialization will cause a segmentation fault by QEMU.  This is emitted
since a declaration of the ``RdV`` register was parsed, but we got no indication
that the variable has been initialized by the caller.

::

   TCGv_i32 tmp_0 = tcg_temp_new_i32();

Then we are declaring a temporary TCGv to hold the result from the sum
operation.

::

   tcg_gen_add_i32(tmp_0, RsV, RtV);

Now we are actually generating the sum tinycode operator between the selected
registers, storing the result in the just declared temporary.

::

   tcg_gen_mov_i32(RdV, tmp_0);

The result of the addition is now stored in the temporary, we move it into the
correct destination register. This might not seem an efficient code, but QEMU
will perform some tinycode optimization, reducing the unnecessary copy.

::

   tcg_temp_free_i32(tmp_0);

Finally, we free the temporary we used to hold the addition result.

Parser Structure
----------------

The idef-parser is built using the ``flex`` and ``bison``.

``flex`` is used to split the input string into tokens, each described using a
regular expression. The token description is contained in the
``idef-parser.lex`` source file. The flex-generated scanner takes care also to
extract from the input text other meaningful information, e.g., the numerical
value in case of an immediate constant, and decorates the token with the
extracted information.

``bison`` is used to generate the actual parser, starting from the parsing
description contained in the ``idef-parser.y`` file. The generated parser
executes the ``main`` function at the end of the ``idef-parser.y`` file, which
opens input and output files, creates the parsing context, and eventually calls
the ``yyparse()`` function, which starts the execution of the LALR(1) parser
(see `Wikipedia <https://en.wikipedia.org/wiki/LALR_parser>`__ for more
information about LALR parsing techniques). The LALR(1) parser, whenever it has
to shift a token, calls the ``yylex()`` function, which is defined by the
flex-generated code, and reads the input file returning the next scanned token.

The tokens are mapped on the source language grammar, defined in the
``idef-parser.y`` file to build a unique syntactic tree, according to the
specified operator precedences and associativity rules.

The grammar describes the whole file which contains the Hexagon instruction
descriptions, therefore it starts from the ``input`` nonterminal, which is a
list of instructions, each instruction is represented by the following grammar
rule, representing the structure of the input file shown above:

::

   instruction : INAME code

   code        : LBR decls statements decls RBR

   statements  : statements statement
               | statement

   statement   : control_statement
               | rvalue SEMI
               | code_block
               | SEMI

   code_block  : LBR statements RBR
               | LBR RBR

With this initial portion of the grammar we are defining the instruction
statements, which are enclosed by the declarations. Each statement can be a
``control_statement``, a code block, which is just a bracket-enclosed list of
statements, a ``SEMI``, which is a ``nop`` instruction, and an ``rvalue SEMI``.

Expressions
~~~~~~~~~~~

``rvalue`` is the nonterminal representing expressions, which are everything
that could be assigned to a variable. ``rvalue SEMI`` can be a statement on its
own because the assign statement, just as in the C language, is itself an
expression.

``rvalue``\ s can be registers, immediates, predicates, control registers,
variables, or any combination of other ``rvalue``\ s through operators. An
``rvalue`` can be either an immediate or a TCGv, the actual type is determined
by the ``t_hex_value.type`` field. In case it is an immediate, its combination
with other immediates can be performed at compile-time (constant folding), only
the result will be written into the code. If the ``rvalue`` instead is a TCGv,
the operations performed on it will have to be emitted as tinycode instructions,
therefore their result will be known only at runtime. An immediate can be copied
into a TCGv through the ``rvalue_materialize`` function, which allocates a
temporary TCGv and copies the value of the immediate in it. Each temporary
should be freed after that it is no more used, we usually free both operands of
each operator, in an SSA fashion.

``lvalue``\ s instead represents all the variables which can be assigned to a
value, and are specialized into registers and variables:

::

   lvalue        : REG
                 | VAR

The effective assignment of ``lvalue``\ s is handled by the ``gen_assign()``
function.

Automatic Variables
~~~~~~~~~~~~~~~~~~~

The input code can contain implicitly declared automatic variables, which are
initialized with a value and then used. We performed a dedicated handling of
such variables, because they will be matched by a generic ``VARID`` token, which
will feature the variable name as a decoration. Each time that the variable is
found, we have to check if that's the first variable use, in that case we
declare a new automatic variable in the tinycode, which can be considered at all
effects as an immediate. Special care is taken to make sure that each variable
is declared only the first time it is seen. Furthermore the variable might
inherit some characteristics like the signedness and the bit width, which must
be propagated from the initialization of the variable to all the further uses of
the variable.

The combination of ``rvalue``\ s are handled through the use of the
``gen_bin_op`` and ``gen_bin_cmp`` helper functions. These two functions handle
the appropriate compile-time or run-time emission of operations to perform the
required computation.

Type System
~~~~~~~~~~~

idef-parser features a simple type system which is used to correctly implement
the signedness and bit width of the operations.

The type of each ``rvalue`` is determined by two attributes: its bit width
(``unsigned bit_width``) and its signedness (``bool is_unsigned``).

For each operation, the type of ``rvalue``\ s influence the way in which the
operands are handled and emitted. For example a right shift between signed
operators will be an algebraic shift, while one between unsigned operators will
be a logical shift. If one of the two operands is signed, and the other is
unsigned, the operation will be signed.

The bit width also influences the outcome of the operations, in particular while
the input languages features a fine granularity type system, with types of 8,
16, 32, 64 (and more for vectorial instructions) bits, the tinycode only
features 32 and 64 bit widths. We propagate as much as possible the fine
granularity type, until the value has to be used inside an operation between
``rvalue``\ s; in that case if one of the two operands is greater than 32 bits
we promote the whole operation to 64 bit, taking care of properly extending the
two operands.  Fortunately, the most critical instructions already feature
explicit casts and zero/sign extensions which are properly propagated down to
our parser.

Control Statements
~~~~~~~~~~~~~~~~~~

``control_statement``\ s are all the statements which modify the order of
execution of the generated code according to input parameters. They are expanded
by the following grammar rule:

::

   control_statement : frame_check
                     | cancel_statement
                     | if_statement
                     | for_statement
                     | fpart1_statement

``if_statement``\ s require the emission of labels and branch instructions which
effectively perform conditional jumps (``tcg_gen_brcondi``) according to the
value of an expression. All the predicated instructions, and in general all the
instructions where there could be alternative values assigned to an ``lvalue``,
like C-style ternary expressions:

::

   rvalue            : rvalue QMARK rvalue COLON rvalue

Are handled using the conditional move tinycode instruction
(``tcg_gen_movcond``), which avoids the additional complexity of managing labels
and jumps.

Instead, regarding the ``for`` loops, exploiting the fact that they always
iterate on immediate values, therefore their iteration ranges are always known
at compile time, we implemented those emitting plain C ``for`` loops. This is
possible because the loops will be executed in the QEMU code, leading to the
consequential unrolling of the for loop, since the tinycode generator
instructions will be executed multiple times, and the respective generated
tinycode will represent the unrolled execution of the loop.

Parsing Context
~~~~~~~~~~~~~~~

All the helper functions in ``idef-parser.y`` carry two fixed parameters, which
are the parsing context ``c`` and the ``YYLLOC`` location information. The
context is explicitly passed to all the functions because the parser we generate
is a reentrant one, meaning that it does not have any global variable, and
therefore the instruction compilation could easily be parallelized in the
future. Finally for each rule we propagate information about the location of the
involved tokens to generate a pretty error reporting, able to highlight the
portion of the input code which generated each error.

Debugging
---------

Developing the idef-parser can lead to two types of errors: compile-time errors
and parsing errors.

Compile-time errors in Bison-generated parsers are usually due to conflicts in
the described grammar. Conflicts forbid the grammar to produce a unique
derivation tree, thus must be solved (except for the dangling else problem,
which is marked as expected through the ``%expect 1`` Bison option).

For solving conflicts you need a basic understanding of `shift-reduce conflicts
<https://www.gnu.org/software/Bison/manual/html_node/Shift_002fReduce.html>`__
and `reduce-reduce conflicts
<https://www.gnu.org/software/Bison/manual/html_node/Reduce_002fReduce.html>`__,
then, if you are using a Bison version > 3.7.1 you can ask Bison to generate
some counterexamples which highlight ambiguous derivations, passing the
``-Wcex`` option to Bison. In general shift/reduce conflicts are solved by
redesigning the grammar in an unambiguous way or by setting the token priority
correctly, while reduce/reduce conflicts are solved by redesigning the
interested part of the grammar.

Run-time errors can be divided between lexing and parsing errors, lexing errors
are hard to detect, since the ``VAR`` token will catch everything which is not
catched by other tokens, but easy to fix, because most of the time a simple
regex editing will be enough.

idef-parser features a fancy parsing error reporting scheme, which for each
parsing error reports the fragment of the input text which was involved in the
parsing rule that generated an error.

Implementing an instruction goes through several sequential steps, here are some
suggestions to make each instruction proceed to the next step.

-  not-emitted

   Means that the parsing of the input code relative to that instruction failed,
   this could be due to a lexical error or to some mismatch between the order of
   valid tokens and a parser rule. You should check that tokens are correctly
   identified and mapped, and that there is a rule matching the token sequence
   that you need to parse.

-  emitted

   This instruction class contains all the instruction which are emitted but
   fail to compile when included in QEMU. The compilation errors are shown by
   the QEMU building process and will lead to fixing the bug.  Most common
   errors regard the mismatch of parameters for tinycode generator functions,
   which boil down to errors in the idef-parser type system.

-  compiled

   These instruction generate valid tinycode generator code, which however fail
   the QEMU or the harness tests, these cases must be handled manually by
   looking into the failing tests and looking at the generated tinycode
   generator instruction and at the generated tinycode itself. Tip: handle the
   failing harness tests first, because they usually feature only a single
   instruction, thus will require less execution trace navigation. If a
   multi-threaded test fail, fixing all the other tests will be the easier
   option, hoping that the multi-threaded one will be indirectly fixed.

-  tests-passed

   This is the final goal for each instruction, meaning that the instruction
   passes the test suite.

Another approach to fix QEMU system test, where many instructions might fail, is
to compare the execution trace of your implementation with the reference
implementations already present in QEMU. To do so you should obtain a QEMU build
where the instruction pass the test, and run it with the following command:

::

   sudo unshare -p sudo -u <USER> bash -c \
   'env -i <qemu-hexagon full path> -d cpu <TEST>'

And do the same for your implementation, the generated execution traces will be
inherently aligned and can be inspected for behavioral differences using the
``diff`` tool.

Limitations and Future Development
----------------------------------

The main limitation of the current parser is given by the syntax-driven nature
of the Bison-generated parsers. This has the severe implication of only being
able to generate code in the order of evaluation of the various rules, without,
in any case, being able to backtrack and alter the generated code.

An example limitation is highlighted by this statement of the input language:

::

   { (PsV==0xff) ? (PdV=0xff) : (PdV=0x00); }

This ternary assignment, when written in this form requires us to emit some
proper control flow statements, which emit a jump to the first or to the second
code block, whose implementation is extremely convoluted, because when matching
the ternary assignment, the code evaluating the two assignments will be already
generated.

Instead we pre-process that statement, making it become:

::

   { PdV = ((PsV==0xff)) ? 0xff : 0x00; }

Which can be easily matched by the following parser rules:

::

   statement             | rvalue SEMI

   rvalue                : rvalue QMARK rvalue COLON rvalue
                         | rvalue EQ rvalue
                         | LPAR rvalue RPAR
                         | assign_statement
                         | IMM

   assign_statement      : pre ASSIGN rvalue

Another example that highlight the limitation of the flex/bison parser can be
found even in the add operation we already saw:

::

   TCGv_i32 tmp_0 = tcg_temp_new_i32();
   tcg_gen_add_i32(tmp_0, RsV, RtV);
   tcg_gen_mov_i32(RdV, tmp_0);

The fact that we cannot directly use ``RdV`` as the destination of the sum is a
consequence of the syntax-driven nature of the parser. In fact when we parse the
assignment, the ``rvalue`` token, representing the sum has already been reduced,
and thus its code emitted and unchangeable. We rely on the fact that QEMU will
optimize our code reducing the useless move operations and the relative
temporaries.

A possible improvement of the parser regards the support for vectorial
instructions and floating point instructions, which will require the extension
of the scanner, the parser, and a partial re-design of the type system, allowing
to build the vectorial semantics over the available vectorial tinycode generator
primitives.

A more radical improvement will use the parser, not to generate directly the
tinycode generator code, but to generate an intermediate representation like the
LLVM IR, which in turn could be compiled using the clang TCG backend. That code
could be furtherly optimized, overcoming the limitations of the syntax-driven
parsing and could lead to a more optimized generated code.
