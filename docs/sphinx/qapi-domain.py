"""
QAPI domain extension.
"""

from __future__ import annotations

from typing import (
    TYPE_CHECKING,
    Any,
    ClassVar,
    Dict,
    Iterable,
    List,
    Tuple,
    cast,
)

from docutils import nodes
from docutils.parsers.rst import directives

from sphinx import addnodes
from sphinx.domains import Domain, ObjType
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective, switch_source_input
from sphinx.util.nodes import make_id, nested_parse_with_titles


if TYPE_CHECKING:
    from docutils.nodes import Element, Node

    from sphinx.application import Sphinx
    from sphinx.util.typing import OptionSpec

logger = logging.getLogger(__name__)


def _nested_parse(directive: SphinxDirective, content_node: Element) -> None:
    """
    This helper preserves error parsing context across sphinx versions.
    """

    # necessary so that the child nodes get the right source/line set
    content_node.document = directive.state.document

    try:
        # Modern sphinx (6.2.0+) supports proper offsetting for
        # nested parse error context management
        nested_parse_with_titles(
            directive.state,
            directive.content,
            content_node,
            content_offset=directive.content_offset,  # type: ignore[call-arg]
        )
    except TypeError:
        # No content_offset argument. Fall back to SSI method.
        with switch_source_input(directive.state, directive.content):
            nested_parse_with_titles(directive.state, directive.content, content_node)


class QAPIModule(SphinxDirective):
    """
    Directive to mark description of a new module.

    This directive doesn't generate any special formatting, and is just
    a pass-through for the content body. Named section titles are
    allowed in the content body.

    Use this directive to associate subsequent definitions with the
    module they are defined in for purposes of search and QAPI index
    organization.

    :arg: The name of the module.
    :opt no-index: Don't add cross-reference targets or index entries.
    :opt no-typesetting: Don't render the content body (but preserve any
       cross-reference target IDs in the squelched output.)

    Example::

       .. qapi:module:: block-core
          :no-index:
          :no-typesetting:

          Lorem ipsum, dolor sit amet ...

    """

    has_content = True
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False

    option_spec: ClassVar[OptionSpec] = {
        # These are universal "Basic" options;
        # https://www.sphinx-doc.org/en/master/usage/domains/index.html#basic-markup
        "no-index": directives.flag,
        "no-typesetting": directives.flag,
        "no-contents-entry": directives.flag,  # NB: No effect
        # Deprecated aliases; to be removed in Sphinx 9.0
        "noindex": directives.flag,
        "nocontentsentry": directives.flag,  # NB: No effect
    }

    def run(self) -> List[Node]:
        modname = self.arguments[0].strip()
        no_index = "no-index" in self.options or "noindex" in self.options

        self.env.ref_context["qapi:module"] = modname

        content_node: Element = nodes.section()
        _nested_parse(self, content_node)

        ret: List[Node] = []
        inode = addnodes.index(entries=[])

        if not no_index:
            node_id = make_id(self.env, self.state.document, "module", modname)
            target = nodes.target("", "", ids=[node_id], ismod=True)
            self.set_source_info(target)
            self.state.document.note_explicit_target(target)

            indextext = f"QAPI module; {modname}"
            inode = addnodes.index(
                entries=[
                    ("pair", indextext, node_id, "", None),
                ]
            )
            ret.append(inode)
            content_node.insert(0, target)

        if "no-typesetting" in self.options:
            if node_ids := [
                node_id
                for el in content_node.findall(nodes.Element)
                for node_id in cast(Iterable[str], el.get("ids", ()))
            ]:
                target = nodes.target(ids=node_ids)
                self.set_source_info(target)
                ret.append(target)
        else:
            ret.extend(content_node.children)

        return ret


class QAPIDomain(Domain):
    """QAPI language domain."""

    name = "qapi"
    label = "QAPI"

    object_types: Dict[str, ObjType] = {}

    # Each of these provides a ReST directive,
    # e.g. .. qapi:module:: block-core
    directives = {
        "module": QAPIModule,
    }

    roles = {}
    initial_data: Dict[str, Dict[str, Tuple[Any]]] = {}
    indices = []

    def merge_domaindata(self, docnames: List[str], otherdata: Dict[str, Any]) -> None:
        pass


def setup(app: Sphinx) -> Dict[str, Any]:
    app.setup_extension("sphinx.directives")
    app.add_domain(QAPIDomain)

    return {
        "version": "1.0",
        "env_version": 1,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
