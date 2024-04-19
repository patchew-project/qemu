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
from sphinx.addnodes import desc_signature, pending_xref
from sphinx.directives import ObjectDescription
from sphinx.domains import (
    Domain,
    Index,
    IndexEntry,
    ObjType,
)
from sphinx.locale import _, __
from sphinx.roles import XRefRole
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective, switch_source_input
from sphinx.util.nodes import (
    make_id,
    make_refnode,
    nested_parse_with_titles,
)


if TYPE_CHECKING:
    from docutils.nodes import Element, Node

    from sphinx.application import Sphinx
    from sphinx.builders import Builder
    from sphinx.environment import BuildEnvironment
    from sphinx.util.typing import OptionSpec

logger = logging.getLogger(__name__)


class ObjectEntry(NamedTuple):
    docname: str
    node_id: str
    objtype: str
    aliased: bool


class QAPIXRefRole(XRefRole):
    def process_link(
        self,
        env: BuildEnvironment,
        refnode: Element,
        has_explicit_title: bool,
        title: str,
        target: str,
    ) -> tuple[str, str]:
        refnode["qapi:module"] = env.ref_context.get("qapi:module")
        if not has_explicit_title:
            title = title.lstrip(".")  # only has a meaning for the target
            target = target.lstrip("~")  # only has a meaning for the title
            # if the first character is a tilde, don't display the module
            # parts of the contents
            if title[0:1] == "~":
                title = title[1:]
                dot = title.rfind(".")
                if dot != -1:
                    title = title[dot + 1 :]
        # if the first character is a dot, search more specific namespaces first
        # else search builtins first
        if target[0:1] == ".":
            target = target[1:]
            refnode["refspecific"] = True
        return title, target


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


# Alias for the return of handle_signature(), which is used in several places.
# (In the Python domain, this is Tuple[str, str] instead.)
Signature = str


class QAPIObject(ObjectDescription[Signature]):
    """
    Description of a generic QAPI object.

    It's not used directly, but is instead subclassed by specific directives.
    """

    # Inherit some standard options from Sphinx's ObjectDescription
    option_spec: OptionSpec = ObjectDescription.option_spec.copy()  # type:ignore[misc]
    option_spec.update(
        {
            # Borrowed from the Python domain:
            "module": directives.unchanged,  # Override contextual module name
        }
    )

    def get_signature_prefix(self, sig: str) -> List[nodes.Node]:
        """Returns a prefix to put before the object name in the signature."""
        assert self.objtype
        return [
            addnodes.desc_sig_keyword("", self.objtype.title()),
            addnodes.desc_sig_space(),
        ]

    def get_signature_suffix(self, sig: str) -> list[nodes.Node]:
        """Returns a suffix to put after the object name in the signature."""
        return []

    def handle_signature(self, sig: str, signode: desc_signature) -> Signature:
        """
        Transform a QAPI definition name into RST nodes.

        This method was originally intended for handling function
        signatures. In the QAPI domain, however, we only pass the
        command name as the directive argument and handle everything
        else in the content body with field lists.

        As such, the only argument here is "sig", which is just the QAPI
        definition name.
        """
        modname = self.options.get("module", self.env.ref_context.get("qapi:module"))

        signode["fullname"] = sig
        signode["module"] = modname
        sig_prefix = self.get_signature_prefix(sig)
        if sig_prefix:
            signode += addnodes.desc_annotation(str(sig_prefix), "", *sig_prefix)
        signode += addnodes.desc_name(sig, sig)
        signode += self.get_signature_suffix(sig)

        return sig

    def _object_hierarchy_parts(self, sig_node: desc_signature) -> Tuple[str, ...]:
        if "fullname" not in sig_node:
            return ()
        modname = sig_node.get("module")
        fullname = sig_node["fullname"]

        if modname:
            return (modname, *fullname.split("."))
        else:
            return tuple(fullname.split("."))

    def get_index_text(self, modname: str, name: Signature) -> str:
        """Return the text for the index entry of the object."""
        # NB this is used for the global index, not the QAPI index.
        return f"{name} (QMP {self.objtype})"

    def add_target_and_index(
        self, name: Signature, sig: str, signode: desc_signature
    ) -> None:
        # Called by ObjectDescription.run with the result of
        # handle_signature; name is the return value of handle_signature
        # where sig is the original argument to handle_signature. In our
        # case, they're the same for now.
        assert self.objtype

        modname = self.options.get("module", self.env.ref_context.get("qapi:module"))
        # Here, sphinx decides to prepend the module name. OK.
        fullname = (modname + "." if modname else "") + name
        node_id = make_id(self.env, self.state.document, "", fullname)
        signode["ids"].append(node_id)
        self.state.document.note_explicit_target(signode)

        domain = cast(QAPIDomain, self.env.get_domain("qapi"))
        domain.note_object(fullname, self.objtype, node_id, location=signode)

        if "no-index-entry" not in self.options:
            indextext = self.get_index_text(modname, name)
            assert self.indexnode is not None
            if indextext:
                self.indexnode["entries"].append(
                    ("single", indextext, node_id, "", None)
                )

    def _toc_entry_name(self, sig_node: desc_signature) -> str:
        # This controls the name in the TOC and on the sidebar.

        # This is the return type of _object_hierarchy_parts().
        toc_parts = cast(Tuple[str, ...], sig_node.get("_toc_parts", ()))
        if not toc_parts:
            return ""

        config = self.env.app.config
        *parents, name = toc_parts
        if config.toc_object_entries_show_parents == "domain":
            return sig_node.get("fullname", name)
        if config.toc_object_entries_show_parents == "hide":
            return name
        if config.toc_object_entries_show_parents == "all":
            return ".".join(parents + [name])
        return ""


class QAPICommand(QAPIObject):
    """Description of a QAPI Command."""

    # Nothing unique for now! Changed in later commits O:-)
    pass


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
        "command": ObjType(_("command"), "cmd", "obj"),
    }

    # Each of these provides a ReST directive,
    # e.g. .. qapi:module:: block-core
    directives = {
        "module": QAPIModule,
        "command": QAPICommand,
    }

    # These are all cross-reference roles; e.g.
    # :qapi:cmd:`query-block`. The keys correlate to the names used in
    # the object_types table values above.
    roles = {
        "mod": QAPIXRefRole(),
        "cmd": QAPIXRefRole(),
        "obj": QAPIXRefRole(),  # reference *any* type of QAPI object.
    }

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

    def find_obj(
        self, modname: str, name: str, type: Optional[str]
    ) -> list[tuple[str, ObjectEntry]]:
        """
        Find a QAPI object for "name", perhaps using the given module.

        Returns a list of (name, object entry) tuples.

        :param modname: The current module context (if any!)
                        under which we are searching.
        :param name: The name of the x-ref to resolve;
                     may or may not include a leading module.
        :param type: The role name of the x-ref we're resolving, if provided.
                     (This is absent for "any" lookups.)
        """
        if not name:
            return []

        names: list[str] = []
        matches: list[tuple[str, ObjectEntry]] = []

        fullname = name
        if "." in fullname:
            # We're searching for a fully qualified reference;
            # ignore the contextual module.
            pass
        elif modname:
            # We're searching for something from somewhere;
            # try searching the current module first.
            # e.g. :qapi:cmd:`query-block` or `query-block` is being searched.
            fullname = f"{modname}.{name}"

        if type is None:
            # type isn't specified, this is a generic xref.
            # search *all* qapi-specific object types.
            objtypes: Optional[List[str]] = list(self.object_types)
        else:
            # type is specified and will be a role (e.g. obj, mod, cmd)
            # convert this to eligible object types (e.g. command, module)
            # using the QAPIDomain.object_types table.
            objtypes = self.objtypes_for_role(type)

        # Either we should have been given no type, or the type we were
        # given should correspond to at least one real actual object
        # type.
        assert objtypes

        if name in self.objects and self.objects[name].objtype in objtypes:
            names = [name]
        elif fullname in self.objects and self.objects[fullname].objtype in objtypes:
            names = [fullname]
        else:
            # exact match wasn't found; e.g. we are searching for
            # `query-block` from a different (or no) module.
            searchname = "." + name
            names = [
                oname
                for oname in self.objects
                if oname.endswith(searchname)
                and self.objects[oname].objtype in objtypes
            ]

        matches = [(oname, self.objects[oname]) for oname in names]
        if len(matches) > 1:
            matches = [m for m in matches if not m[1].aliased]
        return matches

    def resolve_xref(
        self,
        env: BuildEnvironment,
        fromdocname: str,
        builder: Builder,
        type: str,
        target: str,
        node: pending_xref,
        contnode: Element,
    ) -> Element | None:
        modname = node.get("qapi:module")
        matches = self.find_obj(modname, target, type)
        multiple_matches = len(matches) > 1

        if not matches:
            return None
        elif multiple_matches:
            logger.warning(
                __("more than one target found for cross-reference %r: %s"),
                target,
                ", ".join(match[0] for match in matches),
                type="ref",
                subtype="qapi",
                location=node,
            )
        name, obj = matches[0]
        return make_refnode(
            builder, fromdocname, obj.docname, obj.node_id, contnode, name
        )

    def resolve_any_xref(
        self,
        env: BuildEnvironment,
        fromdocname: str,
        builder: Builder,
        target: str,
        node: pending_xref,
        contnode: Element,
    ) -> list[tuple[str, Element]]:
        results: list[tuple[str, Element]] = []
        matches = self.find_obj(node.get("qapi:module"), target, None)
        for name, obj in matches:
            role = "qapi:" + self.role_for_objtype(obj.objtype)
            refnode = make_refnode(
                builder, fromdocname, obj.docname, obj.node_id, contnode, name
            )
            results.append((role, refnode))
        return results


def setup(app: Sphinx) -> Dict[str, Any]:
    app.setup_extension("sphinx.directives")
    app.add_domain(QAPIDomain)

    return {
        "version": "1.0",
        "env_version": 1,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
