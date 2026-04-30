#!/usr/bin/env python3
#
# Mini-Kconfig parser
#
# Copyright (c) 2015 Red Hat Inc.
#
# Authors:
#  Paolo Bonzini <pbonzini@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or, at your option, any later version.  See the COPYING file in
# the top-level directory.

import os
import random
import re
import sys
from dataclasses import dataclass

__all__ = [ 'KconfigDataError', 'KconfigParserError',
            'KconfigData', 'KconfigParser' ,
            'defconfig', 'allyesconfig', 'allnoconfig', 'randconfig' ]

@dataclass
class IncludeInfo:
    file: str
    line: int
    parent: IncludeInfo | None

    def __iter__(self):
        inf = self
        while inf is not None:
            yield "%s:%d" % (inf.file, inf.line)
            inf = inf.parent

    def error_path(self):
        res = ""
        for loc in self:
            res = "In file included from %s:\n" % loc + res
        return res

def debug_print(*args):
    #print('# ' + (' '.join(str(x) for x in args)))
    pass

# -------------------------------------------
# KconfigData implements the Kconfig semantics.  For now it can only
# detect undefined symbols, i.e. symbols that were referenced in
# assignments or dependencies but were not declared with "config FOO".
#
# Semantic actions are represented by methods called do_*.  The do_var
# method return the semantic value of a variable (which right now is
# just its name).
# -------------------------------------------

class KconfigDataError(Exception):
    def __init__(self, msg):
        self.msg = msg

    def __str__(self):
        return self.msg

allyesconfig = lambda x: True
allnoconfig = lambda x: False
defconfig = lambda x: x
randconfig = lambda x: random.randint(0, 1) == 1

class KconfigData:
    class Expr:
        def __and__(self, rhs):
            return KconfigData.AND(self, rhs)
        def __or__(self, rhs):
            return KconfigData.OR(self, rhs)
        def __invert__(self):
            return KconfigData.NOT(self)

        # Abstract methods
        def add_edges_to(self, var):
            pass
        def evaluate(self):
            assert False

    class AND(Expr):
        def __init__(self, lhs, rhs):
            self.lhs = lhs
            self.rhs = rhs
        def __str__(self):
            return "(%s && %s)" % (self.lhs, self.rhs)

        def add_edges_to(self, var):
            self.lhs.add_edges_to(var)
            self.rhs.add_edges_to(var)
        def evaluate(self):
            return self.lhs.evaluate() and self.rhs.evaluate()

    class OR(Expr):
        def __init__(self, lhs, rhs):
            self.lhs = lhs
            self.rhs = rhs
        def __str__(self):
            return "(%s || %s)" % (self.lhs, self.rhs)

        def add_edges_to(self, var):
            self.lhs.add_edges_to(var)
            self.rhs.add_edges_to(var)
        def evaluate(self):
            return self.lhs.evaluate() or self.rhs.evaluate()

    class NOT(Expr):
        def __init__(self, lhs):
            self.lhs = lhs
        def __str__(self):
            return "!%s" % (self.lhs)

        def add_edges_to(self, var):
            self.lhs.add_edges_to(var)
        def evaluate(self):
            return not self.lhs.evaluate()

    class Var(Expr):
        def __init__(self, name):
            self.name = name
            self.value = None
            self.outgoing = set()
            self.clauses_for_var = list()
        def __str__(self):
            return self.name

        def has_value(self):
            return self.value is not None
        def set_value(self, val, clause):
            self.clauses_for_var.append(clause)
            if self.has_value() and self.value != val:
                print("The following clauses were found for " + self.name, file=sys.stderr)
                for i in self.clauses_for_var:
                    print("    " + str(i), file=sys.stderr)
                raise KconfigDataError('contradiction between clauses when setting %s' % self)
            debug_print("=> %s is now %s" % (self.name, val))
            self.value = val

        # depth first search of the dependency graph
        def dfs(self, visited, f):
            if self in visited:
                return
            visited.add(self)
            for v in self.outgoing:
                v.dfs(visited, f)
            f(self)

        def add_edges_to(self, var):
            self.outgoing.add(var)
        def evaluate(self):
            if not self.has_value():
                raise KconfigDataError('cycle found including %s' % self)
            return self.value

    class Clause:
        def __init__(self, dest):
            self.dest = dest
        def priority(self):
            return 0
        def process(self):
            pass

    class AssignmentClause(Clause):
        def __init__(self, dest, value):
            KconfigData.Clause.__init__(self, dest)
            self.value = value
        def __str__(self):
            return "CONFIG_%s=%s" % (self.dest, 'y' if self.value else 'n')

        def process(self):
            self.dest.set_value(self.value, self)

    class DefaultClause(Clause):
        def __init__(self, dest, value, cond=None):
            KconfigData.Clause.__init__(self, dest)
            self.value = value
            self.cond = cond
            if self.cond is not None:
                self.cond.add_edges_to(self.dest)
        def __str__(self):
            value = 'y' if self.value else 'n'
            if self.cond is None:
                return "config %s default %s" % (self.dest, value)
            else:
                return "config %s default %s if %s" % (self.dest, value, self.cond)

        def priority(self):
            # Defaults are processed just before leaving the variable
            return -1
        def process(self):
            if not self.dest.has_value() and \
                    (self.cond is None or self.cond.evaluate()):
                self.dest.set_value(self.value, self)

    class DependsOnClause(Clause):
        def __init__(self, dest, expr):
            KconfigData.Clause.__init__(self, dest)
            self.expr = expr
            self.expr.add_edges_to(self.dest)
        def __str__(self):
            return "config %s depends on %s" % (self.dest, self.expr)

        def process(self):
            if not self.expr.evaluate():
                self.dest.set_value(False, self)

    class SelectClause(Clause):
        def __init__(self, dest, cond):
            KconfigData.Clause.__init__(self, dest)
            self.cond = cond
            self.cond.add_edges_to(self.dest)
        def __str__(self):
            return "select %s if %s" % (self.dest, self.cond)

        def process(self):
            if self.cond.evaluate():
                self.dest.set_value(True, self)

    def __init__(self, value_mangler=defconfig):
        self.value_mangler = value_mangler
        self.previously_included = []
        self.defined_vars = set()
        self.referenced_vars = dict()
        self.clauses = list()

    # semantic analysis -------------

    def check_undefined(self):
        undef = False
        for i in self.referenced_vars:
            if i not in self.defined_vars:
                print("undefined symbol %s" % (i), file=sys.stderr)
                undef = True
        return undef

    def compute_config(self):
        if self.check_undefined():
            raise KconfigDataError("there were undefined symbols")

        debug_print("Input:")
        for clause in self.clauses:
            debug_print(clause)

        debug_print("\nDependency graph:")
        for i in self.referenced_vars:
            debug_print(i, "->", [str(x) for x in self.referenced_vars[i].outgoing])

        # The reverse of the depth-first order is the topological sort
        dfo = dict()
        visited = set()
        debug_print("\n")
        def visit_fn(var):
            debug_print(var, "has DFS number", len(dfo))
            dfo[var] = len(dfo)

        for name, v in self.referenced_vars.items():
            self.do_default(v, False)
            v.dfs(visited, visit_fn)

        # Put higher DFS numbers and higher priorities first.  This
        # places the clauses in topological order and places defaults
        # after assignments and dependencies.
        self.clauses.sort(key=lambda x: (-dfo[x.dest], -x.priority()))

        debug_print("\nSorted clauses:")
        for clause in self.clauses:
            debug_print(clause)
            clause.process()

        debug_print("")
        values = dict()
        for name, v in self.referenced_vars.items():
            debug_print("Evaluating", name)
            values[name] = v.evaluate()

        return values

    # semantic actions -------------

    def do_declaration(self, var):
        if var.name in self.defined_vars:
            raise KconfigDataError('variable "%s" defined twice' % var.name)

        self.defined_vars.add(var.name)

    # var is a string with the variable's name.
    def do_var(self, var):
        if var in self.referenced_vars:
            return self.referenced_vars[var]

        var_obj = self.referenced_vars[var] = KconfigData.Var(var)
        return var_obj

    def do_assignment(self, var, val):
        self.clauses.append(KconfigData.AssignmentClause(var, val))

    def do_cmdline_assignment(self, var, val):
        assert var.startswith("CONFIG_")
        self.do_assignment(self.do_var(var[7:]), val)

    def do_default(self, var, val, cond=None):
        val = self.value_mangler(val)
        self.clauses.append(KconfigData.DefaultClause(var, val, cond))

    def do_depends_on(self, var, expr):
        self.clauses.append(KconfigData.DependsOnClause(var, expr))

    def do_select(self, var, symbol, cond=None):
        cond = (cond & var) if cond is not None else var
        self.clauses.append(KconfigData.SelectClause(symbol, cond))

    def do_imply(self, var, symbol, cond=None):
        # "config X imply Y [if COND]" is the same as
        # "config Y default y if X [&& COND]"
        cond = (cond & var) if cond is not None else var
        self.do_default(symbol, True, cond)

# -------------------------------------------
# KconfigParser implements a recursive descent parser for (simplified)
# Kconfig syntax.
# -------------------------------------------

# tokens table
TOKENS = {}
TOK_NONE = -1
TOK_LPAREN = 0;   TOKENS[TOK_LPAREN] = '"("';
TOK_RPAREN = 1;   TOKENS[TOK_RPAREN] = '")"';
TOK_EQUAL = 2;    TOKENS[TOK_EQUAL] = '"="';
TOK_AND = 3;      TOKENS[TOK_AND] = '"&&"';
TOK_OR = 4;       TOKENS[TOK_OR] = '"||"';
TOK_NOT = 5;      TOKENS[TOK_NOT] = '"!"';
TOK_DEPENDS = 6;  TOKENS[TOK_DEPENDS] = '"depends"';
TOK_ON = 7;       TOKENS[TOK_ON] = '"on"';
TOK_SELECT = 8;   TOKENS[TOK_SELECT] = '"select"';
TOK_IMPLY = 9;    TOKENS[TOK_IMPLY] = '"imply"';
TOK_CONFIG = 10;  TOKENS[TOK_CONFIG] = '"config"';
TOK_DEFAULT = 11; TOKENS[TOK_DEFAULT] = '"default"';
TOK_Y = 12;       TOKENS[TOK_Y] = '"y"';
TOK_N = 13;       TOKENS[TOK_N] = '"n"';
TOK_SOURCE = 14;  TOKENS[TOK_SOURCE] = '"source"';
TOK_BOOL = 15;    TOKENS[TOK_BOOL] = '"bool"';
TOK_IF = 16;      TOKENS[TOK_IF] = '"if"';
TOK_ID = 17;      TOKENS[TOK_ID] = 'identifier';
TOK_EOF = 18;     TOKENS[TOK_EOF] = 'end of file';

class KconfigParserError(Exception):
    def __init__(self, parser, msg, tok=None):
        self.loc = parser.location()
        tok = tok if tok is not None else parser.tok
        if tok != TOK_NONE:
            location = TOKENS[tok] if isinstance(tok, int) else '"%s"' % tok
            msg = '%s before %s' % (msg, location)
        self.msg = msg

    def __str__(self):
        return "%s: %s" % (self.loc, self.msg)

class KconfigParser:

    @classmethod
    def parse(cls, fp, data, incl_info=None):
        cls(fp, data, incl_info).parse_config()

    def __init__(self, fp, data, incl_info):
        self.data = data
        self.incl_info = incl_info
        self.abs_fname = os.path.abspath(fp.name)
        self.fname = fp.name
        self.data.previously_included.append(self.abs_fname)
        src = fp.read()
        if src == '' or src[-1] != '\n':
            src += '\n'
        self.src = src
        self.cursor = 0
        self.line = 1
        self.line_pos = 0
        self.get_token()

    # file management -----

    def location(self):
        col = 1
        for ch in self.src[self.line_pos:self.pos]:
            if ch == '\t':
                col += 8 - ((col - 1) % 8)
            else:
                col += 1
        inf = self.incl_info
        incl_chain = inf.error_path() if inf is not None else ""
        return '%s%s:%d:%d' % (incl_chain, self.fname, self.line, col)

    def do_include(self, include):
        incl_abs_fname = os.path.join(os.path.dirname(self.abs_fname),
                                      include)
        # catch inclusion cycle
        inf = self.incl_info
        while inf:
            if incl_abs_fname == os.path.abspath(inf.file):
                raise KconfigParserError(self, "Inclusion loop for %s"
                                    % include)
            inf = inf.parent

        # skip multiple include of the same file
        if incl_abs_fname in self.data.previously_included:
            return
        try:
            try:
                fp = open(incl_abs_fname, 'rt', encoding='utf-8')
            except IOError as e:
                raise KconfigParserError(self, '%s: %s' % (e.strerror, include))

            inner = IncludeInfo(file=self.fname, line=self.line, parent=self.incl_info)
            type(self).parse(fp, self.data, inner)
        finally:
            fp.close()

    # recursive descent parser -----

    # y_or_n: Y | N
    def parse_y_or_n(self):
        if self.tok == TOK_Y:
            self.get_token()
            return True
        if self.tok == TOK_N:
            self.get_token()
            return False
        raise KconfigParserError(self, 'Expected "y" or "n"')

    # var: ID
    def parse_var(self):
        if self.tok == TOK_ID:
            val = self.val
            self.get_token()
            return self.data.do_var(val)
        else:
            raise KconfigParserError(self, 'Expected identifier')

    # assignment_var: ID (starting with "CONFIG_")
    def parse_assignment_var(self):
        if self.tok == TOK_ID:
            val = self.val
            if not val.startswith("CONFIG_"):
                raise KconfigParserError(self,
                           'Expected identifier starting with "CONFIG_"', TOK_NONE)
            self.get_token()
            return self.data.do_var(val[7:])
        else:
            raise KconfigParserError(self, 'Expected identifier')

    # assignment: var EQUAL y_or_n
    def parse_assignment(self):
        var = self.parse_assignment_var()
        if self.tok != TOK_EQUAL:
            raise KconfigParserError(self, 'Expected "="')
        self.get_token()
        self.data.do_assignment(var, self.parse_y_or_n())

    # primary: NOT primary
    #       | LPAREN expr RPAREN
    #       | var
    def parse_primary(self):
        if self.tok == TOK_NOT:
            self.get_token()
            val = ~self.parse_primary()
        elif self.tok == TOK_LPAREN:
            self.get_token()
            val = self.parse_expr()
            if self.tok != TOK_RPAREN:
                raise KconfigParserError(self, 'Expected ")"')
            self.get_token()
        elif self.tok == TOK_ID:
            val = self.parse_var()
        else:
            raise KconfigParserError(self, 'Expected "!" or "(" or identifier')
        return val

    # disj: primary (OR primary)*
    def parse_disj(self):
        lhs = self.parse_primary()
        while self.tok == TOK_OR:
            self.get_token()
            lhs = lhs | self.parse_primary()
        return lhs

    # expr: disj (AND disj)*
    def parse_expr(self):
        lhs = self.parse_disj()
        while self.tok == TOK_AND:
            self.get_token()
            lhs = lhs & self.parse_disj()
        return lhs

    # condition: IF expr
    #       | empty
    def parse_condition(self):
        if self.tok == TOK_IF:
            self.get_token()
            return self.parse_expr()
        else:
            return None

    # property: DEFAULT y_or_n condition
    #       | DEPENDS ON expr
    #       | SELECT var condition
    #       | BOOL
    def parse_property(self, var):
        if self.tok == TOK_DEFAULT:
            self.get_token()
            val = self.parse_y_or_n()
            cond = self.parse_condition()
            self.data.do_default(var, val, cond)
        elif self.tok == TOK_DEPENDS:
            self.get_token()
            if self.tok != TOK_ON:
                raise KconfigParserError(self, 'Expected "on"')
            self.get_token()
            self.data.do_depends_on(var, self.parse_expr())
        elif self.tok == TOK_SELECT:
            self.get_token()
            symbol = self.parse_var()
            cond = self.parse_condition()
            self.data.do_select(var, symbol, cond)
        elif self.tok == TOK_IMPLY:
            self.get_token()
            symbol = self.parse_var()
            cond = self.parse_condition()
            self.data.do_imply(var, symbol, cond)
        elif self.tok == TOK_BOOL:
            self.get_token()
        else:
            raise KconfigParserError(self, 'Error in recursive descent?')

    # properties: properties property
    #       | /* empty */
    def parse_properties(self, var):
        while self.tok == TOK_DEFAULT or self.tok == TOK_DEPENDS or \
              self.tok == TOK_SELECT or self.tok == TOK_BOOL or \
              self.tok == TOK_IMPLY:
            self.parse_property(var)

        # for nicer error message
        if self.tok != TOK_SOURCE and self.tok != TOK_CONFIG and \
           self.tok != TOK_ID and self.tok != TOK_EOF:
            raise KconfigParserError(self, 'expected "source", "config", identifier, '
                    + '"default", "depends on", "imply" or "select"')

    # declaration: config var properties
    def parse_declaration(self):
        if self.tok == TOK_CONFIG:
            self.get_token()
            var = self.parse_var()
            self.data.do_declaration(var)
            self.parse_properties(var)
        else:
            raise KconfigParserError(self, 'Error in recursive descent?')

    # clause: SOURCE
    #       | declaration
    #       | assignment
    def parse_clause(self):
        if self.tok == TOK_SOURCE:
            val = self.val
            self.get_token()
            self.do_include(val)
        elif self.tok == TOK_CONFIG:
            self.parse_declaration()
        elif self.tok == TOK_ID:
            self.parse_assignment()
        else:
            raise KconfigParserError(self, 'expected "source", "config" or identifier')

    # config: clause+ EOF
    def parse_config(self):
        while self.tok != TOK_EOF:
            self.parse_clause()
        return self.data

    # scanner -----

    def get_token(self):
        while True:
            ch = self.src[self.cursor]
            self.pos = self.cursor
            self.cursor += 1

            self.val = None
            tok = self.scan_token(ch)
            if tok is not None:
                self.tok = tok
                return

    def check_keyword(self, rest):
        if not self.src.startswith(rest, self.cursor):
            return False
        length = len(rest)
        if self.src[self.cursor + length].isalnum() or self.src[self.cursor + length] == '_':
            return False
        self.cursor += length
        return True

    def scan_token(self, ch):
        if ch == '#':
            self.cursor = self.src.find('\n', self.cursor)
            return None
        elif ch == '=':
            return TOK_EQUAL
        elif ch == '(':
            return TOK_LPAREN
        elif ch == ')':
            return TOK_RPAREN
        elif ch == '&' and self.src[self.pos+1] == '&':
            self.cursor += 1
            return TOK_AND
        elif ch == '|' and self.src[self.pos+1] == '|':
            self.cursor += 1
            return TOK_OR
        elif ch == '!':
            return TOK_NOT
        elif ch == 'd' and self.check_keyword("epends"):
            return TOK_DEPENDS
        elif ch == 'o' and self.check_keyword("n"):
            return TOK_ON
        elif ch == 's' and self.check_keyword("elect"):
            return TOK_SELECT
        elif ch == 'i' and self.check_keyword("mply"):
            return TOK_IMPLY
        elif ch == 'c' and self.check_keyword("onfig"):
            return TOK_CONFIG
        elif ch == 'd' and self.check_keyword("efault"):
            return TOK_DEFAULT
        elif ch == 'b' and self.check_keyword("ool"):
            return TOK_BOOL
        elif ch == 'i' and self.check_keyword("f"):
            return TOK_IF
        elif ch == 'y' and self.check_keyword(""):
            return TOK_Y
        elif ch == 'n' and self.check_keyword(""):
            return TOK_N
        elif (ch == 's' and self.check_keyword("ource")) or \
              ch == 'i' and self.check_keyword("nclude"):
            # source FILENAME
            # include FILENAME
            while self.src[self.cursor].isspace():
                self.cursor += 1
            start = self.cursor
            self.cursor = self.src.find('\n', self.cursor)
            self.val = self.src[start:self.cursor]
            return TOK_SOURCE
        elif ch.isalnum():
            # identifier
            while self.src[self.cursor].isalnum() or self.src[self.cursor] == '_':
                self.cursor += 1
            self.val = self.src[self.pos:self.cursor]
            return TOK_ID
        elif ch == '\n':
            if self.cursor == len(self.src):
                return TOK_EOF
            self.line += 1
            self.line_pos = self.cursor
        elif not ch.isspace():
            raise KconfigParserError(self, 'invalid input', ch)

        return None

if __name__ == '__main__':
    argv = sys.argv
    mode = defconfig
    if len(sys.argv) > 1:
        if argv[1] == '--defconfig':
            del argv[1]
        elif argv[1] == '--randconfig':
            random.seed()
            mode = randconfig
            del argv[1]
        elif argv[1] == '--allyesconfig':
            mode = allyesconfig
            del argv[1]
        elif argv[1] == '--allnoconfig':
            mode = allnoconfig
            del argv[1]

    if len(argv) == 1:
        print ("%s: at least one argument is required" % argv[0], file=sys.stderr)
        sys.exit(1)

    if argv[1].startswith('-'):
        print ("%s: invalid option %s" % (argv[0], argv[1]), file=sys.stderr)
        sys.exit(1)

    data = KconfigData(mode)
    external_vars = set()
    for arg in argv[3:]:
        m = re.match(r'^(CONFIG_[A-Z0-9_]+)=([yn]?)$', arg)
        if m is not None:
            name, value = m.groups()
            data.do_cmdline_assignment(name, value == 'y')
            external_vars.add(name[7:])
        else:
            with open(arg, 'rt', encoding='utf-8') as fp:
                KconfigParser.parse(fp, data)

    config = data.compute_config()
    for key in sorted(config.keys()):
        if key not in external_vars and config[key]:
            print ('CONFIG_%s=y' % key)

    deps = open(argv[2], 'wt', encoding='utf-8')
    for fname in data.previously_included:
        print ('%s: %s' % (argv[1], fname), file=deps)
    deps.close()
