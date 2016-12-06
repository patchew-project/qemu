#!/usr/bin/env python
# QAPI texi generator
#
# This work is licensed under the terms of the GNU LGPL, version 2+.
# See the COPYING file in the top-level directory.
"""This script produces the documentation of a qapi schema in texinfo format"""
import re
import sys

import qapi

COMMAND_FMT = """
@deftypefn {type} {{{ret}}} {name} @
{{{args}}}

{body}

@end deftypefn

""".format

ENUM_FMT = """
@deftp Enum {name}

{body}

@end deftp

""".format

STRUCT_FMT = """
@deftp {{{type}}} {name} @
{{{attrs}}}

{body}

@end deftp

""".format

EXAMPLE_FMT = """@example
{code}
@end example
""".format


def subst_strong(doc):
    """Replaces *foo* by @strong{foo}"""
    return re.sub(r'\*([^_\n]+)\*', r'@emph{\1}', doc)


def subst_emph(doc):
    """Replaces _foo_ by @emph{foo}"""
    return re.sub(r'\s_([^_\n]+)_\s', r' @emph{\1} ', doc)


def subst_vars(doc):
    """Replaces @var by @code{var}"""
    return re.sub(r'@([\w-]+)', r'@code{\1}', doc)


def subst_braces(doc):
    """Replaces {} with @{ @}"""
    return doc.replace("{", "@{").replace("}", "@}")


def texi_example(doc):
    """Format @example"""
    doc = subst_braces(doc).strip('\n')
    return EXAMPLE_FMT(code=doc)


def texi_format(doc):
    """
    Format documentation

    Lines starting with:
    - |: generates an @example
    - =: generates @section
    - ==: generates @subsection
    - 1. or 1): generates an @enumerate @item
    - o/*/-: generates an @itemize list
    """
    lines = []
    doc = subst_braces(doc)
    doc = subst_vars(doc)
    doc = subst_emph(doc)
    doc = subst_strong(doc)
    inlist = ""
    lastempty = False
    for line in doc.split('\n'):
        empty = line == ""

        if line.startswith("| "):
            line = EXAMPLE_FMT(code=line[2:])
        elif line.startswith("= "):
            line = "@section " + line[2:]
        elif line.startswith("== "):
            line = "@subsection " + line[3:]
        elif re.match("^([0-9]*[.)]) ", line):
            if not inlist:
                lines.append("@enumerate")
                inlist = "enumerate"
            line = line[line.find(" ")+1:]
            lines.append("@item")
        elif re.match("^[o*-] ", line):
            if not inlist:
                lines.append("@itemize %s" % {'o': "@bullet",
                                              '*': "@minus",
                                              '-': ""}[line[0]])
                inlist = "itemize"
            lines.append("@item")
            line = line[2:]
        elif lastempty and inlist:
            lines.append("@end %s\n" % inlist)
            inlist = ""

        lastempty = empty
        lines.append(line)

    if inlist:
        lines.append("@end %s\n" % inlist)
    return "\n".join(lines)


def texi_args(expr, key="data"):
    """
    Format the functions/structure/events.. arguments/members
    """
    if key not in expr:
        return ""

    args = expr[key]
    arg_list = []
    if isinstance(args, str):
        arg_list.append(args)
    else:
        for name, typ in args.iteritems():
            # optional arg
            if name.startswith("*"):
                name = name[1:]
                arg_list.append("['%s': @t{%s}]" % (name, typ))
            # regular arg
            else:
                arg_list.append("'%s': @t{%s}" % (name, typ))

    return ", ".join(arg_list)


def texi_body(doc):
    """
    Format the body of a symbol documentation:
    - a table of arguments
    - followed by "Returns/Notes/Since/Example" sections
    """
    def _section_order(section):
        return {"Returns": 0,
                "Note": 1,
                "Notes": 1,
                "Since": 2,
                "Example": 3,
                "Examples": 3}[section]

    body = ""
    if doc.args:
        body += "@table @asis\n"
        for arg, section in doc.args.iteritems():
            desc = str(section)
            opt = ''
            if desc.startswith("#optional"):
                desc = desc[10:]
                opt = ' *'
            elif desc.endswith("#optional"):
                desc = desc[:-10]
                opt = ' *'
            body += "@item @code{'%s'}%s\n%s\n" % (arg, opt,
                                                   texi_format(desc))
        body += "@end table\n"
    body += texi_format(str(doc.body))

    sections = sorted(doc.sections, key=lambda i: _section_order(i.name))
    for section in sections:
        name, doc = (section.name, str(section))
        func = texi_format
        if name.startswith("Example"):
            func = texi_example

        body += "\n@quotation %s\n%s\n@end quotation" % \
                (name, func(doc))
    return body


def texi_alternate(expr, doc):
    """
    Format an alternate to texi
    """
    args = texi_args(expr)
    body = texi_body(doc)
    return STRUCT_FMT(type="Alternate",
                      name=doc.symbol,
                      attrs="[ " + args + " ]",
                      body=body)


def texi_union(expr, doc):
    """
    Format a union to texi
    """
    discriminator = expr.get("discriminator")
    if discriminator:
        is_flat = True
        union = "Flat Union"
    else:
        is_flat = False
        union = "Simple Union"
        discriminator = "type"

    attrs = "@{ "
    if is_flat:
        attrs += texi_args(expr, "base") + ", "
    else:
        attrs += "'type': @t{str}, 'data': "
    attrs += "[ '%s' = " % discriminator
    attrs += texi_args(expr, "data") + " ] @}"
    body = texi_body(doc)
    return STRUCT_FMT(type=union,
                      name=doc.symbol,
                      attrs=attrs,
                      body=body)


def texi_enum(expr, doc):
    """
    Format an enum to texi
    """
    for i in expr['data']:
        if i not in doc.args:
            doc.args[i] = ''
    body = texi_body(doc)
    return ENUM_FMT(name=doc.symbol,
                    body=body)


def texi_struct(expr, doc):
    """
    Format a struct to texi
    """
    args = texi_args(expr)
    body = texi_body(doc)
    base = expr.get("base")
    attrs = "@{ "
    if base:
        attrs += "%s" % base
        if args:
            attrs += " + "
    attrs += args + " @}"
    return STRUCT_FMT(type="Struct",
                      name=doc.symbol,
                      attrs=attrs,
                      body=body)


def texi_command(expr, doc):
    """
    Format a command to texi
    """
    args = texi_args(expr)
    ret = expr["returns"] if "returns" in expr else ""
    body = texi_body(doc)
    return COMMAND_FMT(type="Command",
                       name=doc.symbol,
                       ret=ret,
                       args="(" + args + ")",
                       body=body)


def texi_event(expr, doc):
    """
    Format an event to texi
    """
    args = texi_args(expr)
    body = texi_body(doc)
    return COMMAND_FMT(type="Event",
                       name=doc.symbol,
                       ret="",
                       args="(" + args + ")",
                       body=body)


def texi_expr(expr, doc):
    """
    Format an expr to texi
    """
    (kind, _) = expr.items()[0]

    fmt = {"command": texi_command,
           "struct": texi_struct,
           "enum": texi_enum,
           "union": texi_union,
           "alternate": texi_alternate,
           "event": texi_event}[kind]

    return fmt(expr, doc)


def texi(docs):
    """
    Convert QAPI schema expressions to texi documentation
    """
    res = []
    for doc in docs:
        expr = doc.expr
        if not expr:
            res.append(texi_body(doc))
            continue
        try:
            doc = texi_expr(expr, doc)
            res.append(doc)
        except:
            print >>sys.stderr, "error at @%s" % doc.info
            raise

    return '\n'.join(res)


def main(argv):
    """
    Takes schema argument, prints result to stdout
    """
    if len(argv) != 2:
        print >>sys.stderr, "%s: need exactly 1 argument: SCHEMA" % argv[0]
        sys.exit(1)

    schema = qapi.QAPISchema(argv[1])
    print texi(schema.docs)


if __name__ == "__main__":
    main(sys.argv)
