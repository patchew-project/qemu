.. _llm-usage:

Use of AI-generated content
===========================

.. warning::

   Please read the below policy before using AI to contribute code or
   documentation to QEMU.  This applies to ChatGPT, Claude, Copilot,
   Llama, and similar tools.

This document provides guidance around acceptable practices
for using `Large Language Models
<https://en.wikipedia.org/wiki/Large_language_model>`__ (LLMs)
when contributing to QEMU.

The QEMU project does not want to dictate what tools contributors
use for their work on the project.  However, the increasing
prevalence of AI-assisted software development and of LLMs specifically
poses a number of difficult questions.  Risks to open source projects
include maintainer burnout from an increased number of contributions, as
well as the risk to the project from unintentional inclusion of copyrighted
material in the LLM's output.

In order to mitigate these risks, the QEMU project limits the way
in which use of the output of generative AI can be included in
contributions to QEMU.  The main guidelines for use of generative AI
tools are as follows:

- It's fine to use LLMs to answer questions, analyze, distill,
  refine, check, suggest, review. Use of LLMs to *create* is limited.

- If in doubt, disclose any use of AI tools other than simple code
  completion and code review.

- LLMs are allowed as a tool to write *better*, not *faster*.

**For any kind of LLM usage, the expectation is that the human contributor
shall be able to :ref:`understand and explain the changes <write_a_meaningful_commit_message>`,
the design choices, and the rationale behind them.**

.. note:: **Use of AI does not remove the need for authors to comply
          with all other requirements for contribution.**  In particular,
          the ``Signed-off-by`` label in a patch submission is a statement
          that the author takes responsibility for the entire contents of
          the patch, certifying that their patch submission is made in
          accordance with the rules of the :ref:`Developer's Certificate of
          Origin (DCO) <dco>`.

Prohibited use of AI
''''''''''''''''''''

The QEMU community is a collaboration between humans, not AI agent
façades. The following items MUST be written by humans:

- cover letters and commit messages

- responses to reviewer comments

- issue descriptions or comments

Light copy editing of human-written text with an AI is allowed.

An exception is made for defects detected by specialized tooling,
including fuzzers and LLM-assisted defect detection. Such issues must
be reviewed by a human before creation, must be created by a human and
communication with maintainers must be done by a human, but including
the verbatim tool output in the issue description is explicitly allowed.

Repeated violations of these rules may be treated as a :ref:`code_of_conduct`
violation.

Official project agents may be exempted from one or more of these
rules, at the discretion of the QEMU project maintainer(s).

LLM-assisted and LLM-created contributions
''''''''''''''''''''''''''''''''''''''''''

Use of generative AI can be classified in the following buckets:

- "background" assistance, such as completion and code review.  This does
  not need disclosure.

- small LLM-assisted bugfixes (as a rule of thumb, below 10 lines of code)
  are allowed with disclosure.

- use of LLMs to help generating parts of a larger patch---test cases,
  adaptations of existing code, boilerplate code for a new API, a tool to
  help performing mechanical changes, etc.  These are generally allowed;
  disclosure is required for non-trivial, functional code changes.

Large, heavily LLM-assisted contributions where LLMs write large parts
of functional code are currently not allowed.

LLM-created contributions will be held to a higher standard of testing
than human-created ones.  Maintainers have freedom to determine what
constitutes "high quality"; in particular, because LLMs lower the cost
of patch generation, there are no exceptions for "writing the tests seems
hard".

Contributors who submit LLM-created contributions are required to
apply the same judgment that would be applied to other code changes.
They must invest substantial time reviewing the code and, when
applicable, curating individual commits.

The QEMU project explicitly forbids using LLMs to do "clean room"
reimplementation of existing code whose license is not compatible
with the GNU General Public License, version 2 **or later**.

.. _ai-used-for:

Commit messages for AI-assisted changes
'''''''''''''''''''''''''''''''''''''''

Whenever this policy requires disclosure of AI usage, contributing
AI-assisted changes to QEMU requires you to certify that you followed
the instructions in this document and in QEMU's ``AGENTS.md``.
**To do so, include an ``AI-used-for:`` line before the
:ref:`Signed-off-by <code-provenance>` line whenever the policy
requires disclosure [2]_.** By using 'AI-used-for', a contributor
identifies areas of the patch that involved use of AI/LLM, and attests
that their usage of AI/LLMs was in compliance with QEMU policies.

The text of the trailer is one or more of ``code``, ``tests``, or
``docs``, possibly followed by an explanation in parentheses:

.. code-block:: none

     AI-used-for: tests, docs
     AI-used-for: code
     AI-used-for: code (refactoring)
     AI-used-for: code (prototype)

Do not include your prompts or summarize the conversation in the commit
message or cover letter.  Extract any information that is relevant to the
reviewer and include it in commit messages, cover letters or code comments.

QEMU does *not* use ``Assisted-by``, ``Co-authored-by`` or ``Generated-by``
trailers to indicate AI usage.  In particular, do not specify the exact
AI model or tool used to create the commit.

Deterministic tooling (sed, coccinelle, formatters) is out of scope for
the trailer, but should be mentioned in the commit message.

.. [2] ``AI-used-for`` must not be included unless some part of the patch
       *content* was authored by the AI.  It should also not be included for
       "background" usage such as autocomplete or obtaining a pre-review of
       the patch.

Motivation
''''''''''

There is not a consensus within the QEMU project---and likely never will
be---about when/how/where it is acceptable to use AI-based tools.  Many
members of the QEMU project and community find value in AI; many others
feel that its negative impact on society and the climate are severe enough
that no use is acceptable.  Still others are working out their opinion.

In accordance with Software Freedom Conservancy's recommendations:

- the project seeks to both support those contributors who outright reject
  generative AI systems, and to not shun those who choose to use them;

- we consider QEMU's copyleft to be a viable approach to the unanswered
  legal questions about AI-generated contributions and their copyright
  status;

- we encourage contributors not to overuse LLMs, and to *add* them to
  their existing skills and tools instead of replacing them.

A common source of grief for maintainers is the (often valid) impression
that AI enables contributions without understanding the effort they take
for those who process them.  Effectively, this shifts a major part of the
burden from the contributors to the maintainers---the scarcest resource
in any free software project, not just QEMU.  Contributors should be
mindful of this and take into account the demand they put on maintainers
with LLM-assisted work.


Sources
'''''''

This policy was informed by:

- `Recommendations When Using LLM-backed Generative AI Systems for
  FOSS Contributions <https://sfconservancy.org/llm-gen-ai/llm-backed-generative-ai-recommendations.html>`__

- `GStreamer AGENTS.md file <https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/d08375499c279/AGENTS.md>`__

