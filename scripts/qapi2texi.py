#!/usr/bin/env python
# QAPI texi generator
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""This script produces the documentation of a qapi schema in texinfo format"""
import re
import sys

from qapi import QAPISchemaParser, QAPISchemaError, check_exprs, QAPIExprError

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
@deftp {type} {name} @
{{{attrs}}}

{body}

@end deftp

""".format

EXAMPLE_FMT = """@example
{code}
@end example
""".format


def subst_emph(doc):
    """Replaces *foo* by @emph{foo}"""
    return re.sub(r'\*(\w+)\*', r'@emph{\1}', doc)


def subst_vars(doc):
    """Replaces @var by @var{var}"""
    return re.sub(r'@(\w+)', r'@var{\1}', doc)


def subst_braces(doc):
    """Replaces {} with @{ @}"""
    return doc.replace("{", "@{").replace("}", "@}")


def texi_example(doc):
    """Format @example"""
    doc = subst_braces(doc).strip('\n')
    return EXAMPLE_FMT(code=doc)


def texi_comment(doc):
    """
    Format a comment

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
    inlist = ""
    lastempty = False
    for line in doc.split('\n'):
        empty = line == ""

        if line.startswith("| "):
            line = EXAMPLE_FMT(code=line[1:])
        elif line.startswith("= "):
            line = "@section " + line[1:]
        elif line.startswith("== "):
            line = "@subsection " + line[2:]
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


def texi_args(expr):
    """
    Format the functions/structure/events.. arguments/members
    """
    data = expr["data"] if "data" in expr else {}
    if isinstance(data, str):
        args = data
    else:
        arg_list = []
        for name, typ in data.iteritems():
            # optional arg
            if name.startswith("*"):
                name = name[1:]
                arg_list.append("['%s': @var{%s}]" % (name, typ))
            # regular arg
            else:
                arg_list.append("'%s': @var{%s}" % (name, typ))
        args = ", ".join(arg_list)
    return args


def texi_body(doc, arg="@var"):
    """
    Format the body of a symbol documentation:
    - a table of arguments
    - followed by "Returns/Notes/Since/Example" sections
    """
    body = "@table %s\n" % arg
    for arg, desc in doc.args.iteritems():
        if desc.startswith("#optional"):
            desc = desc[10:]
            arg += "*"
        elif desc.endswith("#optional"):
            desc = desc[:-10]
            arg += "*"
        body += "@item %s\n%s\n" % (arg, texi_comment(desc))
    body += "@end table\n"
    body += texi_comment(doc.comment)

    for k in ("Returns", "Note", "Notes", "Since", "Example", "Examples"):
        if k not in doc.meta:
            continue
        func = texi_comment
        if k in ("Example", "Examples"):
            func = texi_example
        body += "\n@quotation %s\n%s\n@end quotation" % \
                (k, func(doc.meta[k]))
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
    Format an union to texi
    """
    args = texi_args(expr)
    body = texi_body(doc)
    return STRUCT_FMT(type="Union",
                      name=doc.symbol,
                      attrs="[ " + args + " ]",
                      body=body)


def texi_enum(_, doc):
    """
    Format an enum to texi
    """
    body = texi_body(doc, "@samp")
    return ENUM_FMT(name=doc.symbol,
                    body=body)


def texi_struct(expr, doc):
    """
    Format a struct to texi
    """
    args = texi_args(expr)
    body = texi_body(doc)
    return STRUCT_FMT(type="Struct",
                      name=doc.symbol,
                      attrs="@{ " + args + " @}",
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


def texi(exprs):
    """
    Convert QAPI schema expressions to texi documentation
    """
    res = []
    for qapi in exprs:
        try:
            docs = qapi['info']['doc']
            expr = qapi['expr']
            expr_doc = docs[-1]
            body = docs[0:-1]

            (kind, _) = expr.items()[0]

            for doc in body:
                res.append(texi_body(doc))

            fmt = {"command": texi_command,
                   "struct": texi_struct,
                   "enum": texi_enum,
                   "union": texi_union,
                   "alternate": texi_alternate,
                   "event": texi_event}
            try:
                fmt = fmt[kind]
            except KeyError:
                raise ValueError("Unknown expression kind '%s'" % kind)
            res.append(fmt(expr, expr_doc))
        except:
            print >>sys.stderr, "error at @%s" % qapi
            raise

    return '\n'.join(res)


def parse_schema(fname):
    """
    Parse the given schema file and return the exprs
    """
    try:
        schema = QAPISchemaParser(open(fname, "r"))
        check_exprs(schema.exprs)
        return schema.exprs
    except (QAPISchemaError, QAPIExprError), err:
        print >>sys.stderr, err
        exit(1)


def main(argv):
    """
    Takes argv arguments, prints result to stdout
    """
    if len(argv) != 4:
        print >>sys.stderr, "%s: need exactly 3 arguments: " \
            "TEMPLATE VERSION SCHEMA" % argv[0]
        sys.exit(1)

    exprs = parse_schema(argv[3])

    templ = open(argv[1])
    qapi = texi(exprs)
    print templ.read().format(version=argv[2], qapi=qapi)


if __name__ == "__main__":
    main(sys.argv)
