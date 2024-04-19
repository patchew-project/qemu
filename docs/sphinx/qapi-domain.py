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
    NamedTuple,
    Optional,
    Tuple,
    cast,
)

from docutils import nodes
from docutils.parsers.rst import directives

from sphinx import addnodes
from sphinx.domains import (
    Domain,
    Index,
    IndexEntry,
    ObjType,
)
from sphinx.locale import _, __
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective, switch_source_input
from sphinx.util.nodes import make_id, nested_parse_with_titles


if TYPE_CHECKING:
    from docutils.nodes import Element, Node

    from sphinx.application import Sphinx
    from sphinx.util.typing import OptionSpec

logger = logging.getLogger(__name__)


class ObjectEntry(NamedTuple):
    docname: str
    node_id: str
    objtype: str
    aliased: bool


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

    Use this directive to create entries for the QAPI module in the
    global index and the qapi index; as well as to associate subsequent
    definitions with the module they are defined in for purposes of
    search and QAPI index organization.

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
        domain = cast(QAPIDomain, self.env.get_domain("qapi"))
        modname = self.arguments[0].strip()
        no_index = "no-index" in self.options or "noindex" in self.options

        self.env.ref_context["qapi:module"] = modname

        content_node: Element = nodes.section()
        _nested_parse(self, content_node)

        ret: List[Node] = []
        inode = addnodes.index(entries=[])

        if not no_index:
            # note module to the domain
            node_id = make_id(self.env, self.state.document, "module", modname)
            target = nodes.target("", "", ids=[node_id], ismod=True)
            self.set_source_info(target)
            self.state.document.note_explicit_target(target)

            domain.note_object(modname, "module", node_id, location=target)

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


class QAPIIndex(Index):
    """
    Index subclass to provide the QAPI definition index.
    """

    name = "index"
    localname = _("QAPI Index")
    shortname = _("QAPI Index")

    def generate(
        self,
        docnames: Optional[Iterable[str]] = None,
    ) -> Tuple[List[Tuple[str, List[IndexEntry]]], bool]:
        assert isinstance(self.domain, QAPIDomain)
        content: Dict[str, List[IndexEntry]] = {}
        collapse = False

        # list of all object (name, ObjectEntry) pairs, sorted by name
        objects = sorted(self.domain.objects.items(), key=lambda x: x[0].lower())

        for objname, obj in objects:
            if docnames and obj.docname not in docnames:
                continue

            # Strip the module name out:
            objname = objname.split(".")[-1]

            # Add an alphabetical entry:
            entries = content.setdefault(objname[0].upper(), [])
            entries.append(
                IndexEntry(objname, 0, obj.docname, obj.node_id, obj.objtype, "", "")
            )

            # Add a categorical entry:
            category = obj.objtype.title() + "s"
            entries = content.setdefault(category, [])
            entries.append(IndexEntry(objname, 0, obj.docname, obj.node_id, "", "", ""))

        # alphabetically sort categories; type names first, ABC entries last.
        sorted_content = sorted(
            content.items(),
            key=lambda x: (len(x[0]) == 1, x[0]),
        )
        return sorted_content, collapse


class QAPIDomain(Domain):
    """QAPI language domain."""

    name = "qapi"
    label = "QAPI"

    # This table associates cross-reference object types (key) with an
    # ObjType instance, which defines the valid cross-reference roles
    # for each object type.
    object_types: Dict[str, ObjType] = {
        "module": ObjType(_("module"), "mod", "obj"),
    }

    # Each of these provides a ReST directive,
    # e.g. .. qapi:module:: block-core
    directives = {
        "module": QAPIModule,
    }

    roles = {}

    # Moved into the data property at runtime;
    # this is the internal index of reference-able objects.
    initial_data: Dict[str, Dict[str, Tuple[Any]]] = {
        "objects": {},  # fullname -> ObjectEntry
    }

    # Index pages to generate; each entry is an Index class.
    indices = [
        QAPIIndex,
    ]

    @property
    def objects(self) -> Dict[str, ObjectEntry]:
        return self.data.setdefault("objects", {})  # type: ignore[no-any-return]

    def note_object(
        self,
        name: str,
        objtype: str,
        node_id: str,
        aliased: bool = False,
        location: Any = None,
    ) -> None:
        """Note a QAPI object for cross reference."""
        if name in self.objects:
            other = self.objects[name]
            if other.aliased and aliased is False:
                # The original definition found. Override it!
                pass
            elif other.aliased is False and aliased:
                # The original definition is already registered.
                return
            else:
                # duplicated
                logger.warning(
                    __(
                        "duplicate object description of %s, "
                        "other instance in %s, use :no-index: for one of them"
                    ),
                    name,
                    other.docname,
                    location=location,
                )
        self.objects[name] = ObjectEntry(self.env.docname, node_id, objtype, aliased)

    def clear_doc(self, docname: str) -> None:
        for fullname, obj in list(self.objects.items()):
            if obj.docname == docname:
                del self.objects[fullname]

    def merge_domaindata(self, docnames: List[str], otherdata: Dict[str, Any]) -> None:
        for fullname, obj in otherdata["objects"].items():
            if obj.docname in docnames:
                # FIXME: Unsure of the implications of merge conflicts.
                # Sphinx's own python domain doesn't appear to bother.
                assert (
                    fullname not in self.objects
                ), f"!?!? collision on merge? {fullname=} {obj=} {self.objects[fullname]=}"
                self.objects[fullname] = obj


def setup(app: Sphinx) -> Dict[str, Any]:
    app.setup_extension("sphinx.directives")
    app.add_domain(QAPIDomain)

    return {
        "version": "1.0",
        "env_version": 1,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
