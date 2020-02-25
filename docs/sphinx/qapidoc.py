# coding=utf-8
#
# QEMU qapidoc QAPI file parsing extension
#
# Copyright (c) 2020 Linaro
#
# This work is licensed under the terms of the GNU GPLv2 or later.
# See the COPYING file in the top-level directory.
"""qapidoc is a Sphinx extension that implements the qapi-doc directive"""

# The purpose of this extension is to read the documentation comments
# in QAPI JSON schema files, and insert them all into the current document.
# The conf.py file must set the qapidoc_srctree config value to
# the root of the QEMU source tree.
# Each qapi-doc:: directive takes one argument which is the
# path of the .json file to process, relative to the source tree.

import os
import re

from docutils import nodes
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
from sphinx.errors import ExtensionError
from sphinx.util.nodes import nested_parse_with_titles
import sphinx
from qapi.gen import QAPISchemaVisitor
from qapi.schema import QAPIError, QAPISchema

# Sphinx up to 1.6 uses AutodocReporter; 1.7 and later
# use switch_source_input. Check borrowed from kerneldoc.py.
Use_SSI = sphinx.__version__[:3] >= '1.7'
if Use_SSI:
    from sphinx.util.docutils import switch_source_input
else:
    from sphinx.ext.autodoc import AutodocReporter


__version__ = '1.0'

# Function borrowed from pydash, which is under the MIT license
def intersperse(iterable, separator):
    """Like join, but for arbitrary iterables, notably arrays"""
    iterable = iter(iterable)
    yield next(iterable)
    for item in iterable:
        yield separator
        yield item

class QAPISchemaGenRSTVisitor(QAPISchemaVisitor):
    """A QAPI schema visitor which generates docutils/Sphinx nodes

    This class builds up a tree of docutils/Sphinx nodes corresponding
    to documentation for the various QAPI objects. To use it, first create
    a QAPISchemaGenRSTVisitor object, and call its visit_begin() method.
    Then you can call one of the two methods 'freeform' (to add documentation
    for a freeform documentation chunk) or 'symbol' (to add documentation
    for a QAPI symbol). These will cause the visitor to build up the
    tree of document nodes. Once you've added all the documentation
    via 'freeform' and 'symbol' method calls, you can call 'get_document_nodes'
    to get the final list of document nodes (in a form suitable for returning
    from a Sphinx directive's 'run' method).
    """
    def __init__(self, sphinx_directive):
        self._cur_doc = None
        self._sphinx_directive = sphinx_directive
        self._top_node = nodes.section()
        self._active_headings = [self._top_node]

    def _serror(self, msg):
        """Raise an exception giving a user-friendly syntax error message"""
        file = self._cur_doc.info.fname
        line = self._cur_doc.info.line
        raise ExtensionError('%s line %d: syntax error: %s' % (file, line, msg))

    def _make_dlitem(self, term, defn):
        """Return a dlitem node with the specified term and definition.

        term should be a list of Text and literal nodes.
        defn should be one of:
        - a string, which will be handed to _parse_text_into_node
        - a list of Text and literal nodes, which will be put into
          a paragraph node
        """
        dlitem = nodes.definition_list_item()
        dlterm = nodes.term('', '', *term)
        dlitem += dlterm
        if defn:
            dldef = nodes.definition()
            if isinstance(defn, list):
                dldef += nodes.paragraph('', '', *defn)
            else:
                self._parse_text_into_node(defn, dldef)
            dlitem += dldef
        return dlitem

    def _make_section(self, title):
        """Return a section node with optional title"""
        section = nodes.section(ids=[self._sphinx_directive.new_serialno()])
        if title:
            section += nodes.title(title, title)
        return section

    def _nodes_for_ifcond(self, ifcond, with_if=True):
        """Return list of Text, literal nodes for the ifcond

        Return a list which gives text like ' (If: cond1, cond2, cond3)', where
        the conditions are in literal-text and the commas are not.
        If with_if is False, we don't return the "(If: " and ")".
        """
        condlist = intersperse([nodes.literal('', c) for c in ifcond],
                               nodes.Text(', '))
        if not with_if:
            return condlist

        nodelist = [nodes.Text(' ('), nodes.strong('', 'If: ')]
        nodelist.extend(condlist)
        nodelist.append(nodes.Text(')'))
        return nodelist

    def _nodes_for_one_member(self, member):
        """Return list of Text, literal nodes for this member

        Return a list of doctree nodes which give text like
        'name: type (optional) (If: ...)' suitable for use as the
        'term' part of a definition list item.
        """
        term = [nodes.literal('', member.name)]
        if member.type.doc_type():
            term.append(nodes.Text(': '))
            term.append(nodes.literal('', member.type.doc_type()))
        if member.optional:
            term.append(nodes.Text(' (optional)'))
        if member.ifcond:
            term.extend(self._nodes_for_ifcond(member.ifcond))
        return term

    def _nodes_for_variant_when(self, variants, variant):
        """Return list of Text, literal nodes for variant 'when' clause

        Return a list of doctree nodes which give text like
        'when tagname is variant (If: ...)' suitable for use in
        the 'variants' part of a definition list.
        """
        term = [nodes.Text(' when '),
                nodes.literal('', variants.tag_member.name),
                nodes.Text(' is '),
                nodes.literal('', '"%s"' % variant.name)]
        if variant.ifcond:
            term.extend(self._nodes_for_ifcond(variant.ifcond))
        return term

    def _nodes_for_members(self, doc, what, base=None, variants=None):
        """Return doctree nodes for the table of members"""
        dlnode = nodes.definition_list()
        for section in doc.args.values():
            term = self._nodes_for_one_member(section.member)
            # TODO drop fallbacks when undocumented members are outlawed
            if section.text:
                defn = section.text
            elif (variants and variants.tag_member == section.member
                  and not section.member.type.doc_type()):
                values = section.member.type.member_names()
                defn = [nodes.Text('One of ')]
                defn.extend(intersperse([nodes.literal('', v) for v in values],
                                        nodes.Text(', ')))
            else:
                defn = [nodes.Text('Not documented')]

            dlnode += self._make_dlitem(term, defn)

        if base:
            dlnode += self._make_dlitem([nodes.Text('The members of '),
                                         nodes.literal('', base.doc_type())],
                                        None)

        if variants:
            for v in variants.variants:
                if v.type.is_implicit():
                    assert not v.type.base and not v.type.variants
                    for m in v.type.local_members:
                        term = self._nodes_for_one_member(m)
                        term.extend(self._nodes_for_variant_when(variants, v))
                        dlnode += self._make_dlitem(term, None)
                else:
                    term = [nodes.Text('The members of '),
                            nodes.literal('', v.type.doc_type())]
                    term.extend(self._nodes_for_variant_when(variants, v))
                    dlnode += self._make_dlitem(term, None)

        if not dlnode.children:
            return None

        section = self._make_section(what)
        section += dlnode
        return section

    def _nodes_for_enum_values(self, doc, what):
        """Return doctree nodes for the table of enum values"""
        seen_item = False
        dlnode = nodes.definition_list()
        for section in doc.args.values():
            termtext = [nodes.literal('', section.member.name)]
            if section.member.ifcond:
                termtext.extend(self._nodes_for_ifcond(section.member.ifcond))
            # TODO drop fallbacks when undocumented members are outlawed
            if section.text:
                defn = section.text
            else:
                defn = [nodes.Text('Not documented')]

            dlnode += self._make_dlitem(termtext, defn)
            seen_item = True

        if not seen_item:
            return None

        section = self._make_section(what)
        section += dlnode
        return section

    def _nodes_for_arguments(self, doc, boxed_arg_type):
        """Return doctree nodes for the arguments section"""
        if boxed_arg_type:
            assert not doc.args
            section = self._make_section('Arguments')
            dlnode = nodes.definition_list()
            dlnode += self._make_dlitem(
                [nodes.Text('The members of '),
                 nodes.literal('', boxed_arg_type.name)],
                None)
            section += dlnode
            return section

        return self._nodes_for_members(doc, 'Arguments')

    def _nodes_for_features(self, doc):
        """Return doctree nodes for the table of features"""
        seen_item = False
        dlnode = nodes.definition_list()
        for section in doc.features.values():
            dlnode += self._make_dlitem([nodes.literal('', section.name)],
                                        section.text)
            seen_item = True

        if not seen_item:
            return None

        section = self._make_section('Features')
        section += dlnode
        return section

    def _nodes_for_example(self, exampletext):
        """Return doctree nodes for a code example snippet"""
        return nodes.literal_block(exampletext, exampletext)

    def _nodes_for_sections(self, doc, ifcond):
        """Return doctree nodes for additional sections following arguments"""
        nodelist = []
        for section in doc.sections:
            snode = self._make_section(section.name)
            if section.name and section.name.startswith('Example'):
                snode += self._nodes_for_example(section.text)
            else:
                self._parse_text_into_node(section.text, snode)
            nodelist.append(snode)
        if ifcond:
            snode = self._make_section('If')
            snode += self._nodes_for_ifcond(ifcond, with_if=False)
            nodelist.append(snode)
        if not nodelist:
            return None
        return nodelist

    def _add_doc(self, typ, sections):
        """Add documentation for a command/object/enum...

        We assume we're documenting the thing defined in self._cur_doc.
        typ is the type of thing being added ("Command", "Object", etc)

        sections is a list of nodes for sections to add to the definition.
        """

        doc = self._cur_doc
        snode = nodes.section(ids=[self._sphinx_directive.new_serialno()])
        snode += nodes.title('', '', *[nodes.literal(doc.symbol, doc.symbol),
                                       nodes.Text(' (' + typ + ')')])
        self._parse_text_into_node(doc.body.text, snode)
        for s in sections:
            if s is not None:
                snode += s
        self._add_node_to_current_heading(snode)

    def visit_enum_type(self, name, info, ifcond, members, prefix):
        doc = self._cur_doc
        self._add_doc('Enum',
                      [self._nodes_for_enum_values(doc, 'Values'),
                       self._nodes_for_features(doc),
                       self._nodes_for_sections(doc, ifcond)])

    def visit_object_type(self, name, info, ifcond, base, members, variants,
                          features):
        doc = self._cur_doc
        if base and base.is_implicit():
            base = None
        self._add_doc('Object',
                      [self._nodes_for_members(doc, 'Members', base, variants),
                       self._nodes_for_features(doc),
                       self._nodes_for_sections(doc, ifcond)])

    def visit_alternate_type(self, name, info, ifcond, variants):
        doc = self._cur_doc
        self._add_doc('Alternate',
                      [self._nodes_for_members(doc, 'Members'),
                       self._nodes_for_features(doc),
                       self._nodes_for_sections(doc, ifcond)])

    def visit_command(self, name, info, ifcond, arg_type, ret_type, gen,
                      success_response, boxed, allow_oob, allow_preconfig,
                      features):
        doc = self._cur_doc
        self._add_doc('Command',
                      [self._nodes_for_arguments(doc,
                                                 arg_type if boxed else None),
                       self._nodes_for_features(doc),
                       self._nodes_for_sections(doc, ifcond)])

    def visit_event(self, name, info, ifcond, arg_type, boxed):
        doc = self._cur_doc
        self._add_doc('Event',
                      [self._nodes_for_arguments(doc,
                                                 arg_type if boxed else None),
                       self._nodes_for_features(doc),
                       self._nodes_for_sections(doc, ifcond)])

    def symbol(self, doc, entity):
        """Add documentation for one symbol to the document tree

        This is the main entry point which causes us to add documentation
        nodes for a symbol (which could be a 'command', 'object', 'event',
        etc). We do this by calling 'visit' on the schema entity, which
        will then call back into one of our visit_* methods, depending
        on what kind of thing this symbol is.
        """
        self._cur_doc = doc
        entity.visit(self)
        self._cur_doc = None

    def _start_new_heading(self, heading, level):
        """Start a new heading at the specified heading level

        Create a new section whose title is 'heading' and which is placed
        in the docutils node tree as a child of the most recent level-1
        heading. Subsequent document sections (commands, freeform doc chunks,
        etc) will be placed as children of this new heading section.
        """
        if len(self._active_headings) < level:
            self._serror('Level %d subheading found outside a level %d heading'
                         % (level, level - 1))
        snode = self._make_section(heading)
        self._active_headings[level - 1] += snode
        self._active_headings = self._active_headings[:level]
        self._active_headings.append(snode)

    def _add_node_to_current_heading(self, node):
        """Add the node to whatever the current active heading is"""
        self._active_headings[-1] += node

    def freeform(self, doc):
        """Add a piece of 'freeform' documentation to the document tree

        A 'freeform' document chunk doesn't relate to any particular
        symbol (for instance, it could be an introduction).

        As a special case, if the freeform document is a single line
        of the form '= Heading text' it is treated as a section or subsection
        heading, with the heading level indicated by the number of '=' signs.
        """

        # QAPIDoc documentation says free-form documentation blocks
        # must have only a body section, nothing else.
        assert not doc.sections
        assert not doc.args
        assert not doc.features
        self._cur_doc = doc

        if re.match(r'=+ ', doc.body.text):
            # Section or subsection heading: must be the only thing in the block
            (heading, _, rest) = doc.body.text.partition('\n')
            if rest != '':
                raise ExtensionError('%s line %s: section or subsection heading'
                                     ' must be in its own doc comment block'
                                     % (doc.info.fname, doc.info.line))
            (leader, _, heading) = heading.partition(' ')
            self._start_new_heading(heading, len(leader))
            return

        node = self._make_section(None)
        self._parse_text_into_node(doc.body.text, node)
        self._add_node_to_current_heading(node)
        self._cur_doc = None

    def _parse_text_into_node(self, doctext, node):
        """Parse a chunk of QAPI-doc-format text into the node

        The doc comment can contain most inline rST markup, including
        bulleted and enumerated lists.
        As an extra permitted piece of markup, @var will be turned
        into ``var``.
        """

        # Handle the "@var means ``var`` case
        doctext = re.sub(r'@([\w-]+)', r'``\1``', doctext)

        rstlist = ViewList()
        for line in doctext.splitlines():
            # The reported line number will always be that of the start line
            # of the doc comment, rather than the actual location of the error.
            # Being more precise would require overhaul of the QAPIDoc class
            # to track lines more exactly within all the sub-parts of the doc
            # comment, as well as counting lines here.
            rstlist.append(line, self._cur_doc.info.fname,
                           self._cur_doc.info.line)
        self._sphinx_directive.do_parse(rstlist, node)

    def get_document_nodes(self):
        """Return the list of docutils nodes which make up the document"""
        return self._top_node.children

class QAPIDocDirective(Directive):
    """Extract documentation from the specified QAPI .json file"""
    required_argument = 1
    optional_arguments = 1
    option_spec = {
        'qapifile': directives.unchanged_required
    }
    has_content = False

    def new_serialno(self):
        """Return a unique new ID string suitable for use as a node's ID"""
        env = self.state.document.settings.env
        return 'qapidoc-%d' % env.new_serialno('qapidoc')

    def run(self):
        env = self.state.document.settings.env
        qapifile = env.config.qapidoc_srctree + '/' + self.arguments[0]

        # Tell sphinx of the dependency
        env.note_dependency(os.path.abspath(qapifile))

        try:
            schema = QAPISchema(qapifile)
        except QAPIError as err:
            # Launder QAPI parse errors into Sphinx extension errors
            # so they are displayed nicely to the user
            raise ExtensionError(str(err))

        vis = QAPISchemaGenRSTVisitor(self)
        vis.visit_begin(schema)
        for doc in schema.docs:
            if doc.symbol:
                vis.symbol(doc, schema.lookup_entity(doc.symbol))
            else:
                vis.freeform(doc)

        return vis.get_document_nodes()

    def do_parse(self, rstlist, node):
        """Parse rST source lines and add them to the specified node

        Take the list of rST source lines rstlist, parse them as
        rST, and add the resulting docutils nodes as children of node.
        The nodes are parsed in a way that allows them to include
        subheadings (titles) without confusing the rendering of
        anything else.
        """
        # This is from kerneldoc.py -- it works around an API change in
        # Sphinx between 1.6 and 1.7. Unlike kerneldoc.py, we use
        # sphinx.util.nodes.nested_parse_with_titles() rather than the
        # plain self.state.nested_parse(), and so we can drop the saving
        # of title_styles and section_level that kerneldoc.py does,
        # because nested_parse_with_titles() does that for us.
        if Use_SSI:
            with switch_source_input(self.state, rstlist):
                nested_parse_with_titles(self.state, rstlist, node)
        else:
            save = self.state.memo.reporter
            self.state.memo.reporter = AutodocReporter(rstlist,
                                                       self.state.memo.reporter)
            try:
                nested_parse_with_titles(self.state, rstlist, node)
            finally:
                self.state.memo.reporter = save

def setup(app):
    """ Register qapi-doc directive with Sphinx"""
    app.add_config_value('qapidoc_srctree', None, 'env')
    app.add_directive('qapi-doc', QAPIDocDirective)

    return dict(
        version=__version__,
        parallel_read_safe=True,
        parallel_write_safe=True
    )
