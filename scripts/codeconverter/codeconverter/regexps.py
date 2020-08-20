"""Helpers for creation of regular expressions"""
import re

import logging
logger = logging.getLogger(__name__)
DBG = logger.debug
INFO = logger.info
WARN = logger.warning

def S(*regexps) -> str:
    """Just a shortcut to concatenate multiple regexps more easily"""
    return ''.join(regexps)

def P(*regexps, name=None, capture=False, repeat='') -> str:
    """Just add parenthesis around regexp(s), with optional name or repeat suffix"""
    s = S(*regexps)
    if name:
        return f'(?P<{name}>{s}){repeat}'
    elif capture:
        return f'({s}){repeat}'
    else:
        return f'(?:{s}){repeat}'

def NAMED(name, *regexps) -> str:
    """Make named group using <P<name>...) syntax

    >>> NAMED('mygroup', 'xyz', 'abc')
    '(?P<mygroup>xyzabc)'
    """
    return P(*regexps, name=name)

def OR(*regexps, **kwargs) -> str:
    """Build (a|b|c) regexp"""
    return P('|'.join(regexps), **kwargs)

def M(*regexps, n='*', name=None) -> str:
    """Add repetition qualifier to regexp(s)

    >>> M('a', 'b')
    '(?:ab)*'
    >>> M('a' , 'b', n='+')
    '(?:ab)+'
    >>> M('a' , 'b', n='{2,3}', name='name')
    '(?P<name>(?:ab){2,3})'
    """
    r = P(*regexps, repeat=n)
    if name:
        r = NAMED(name, r)
    return r

# helper to make parenthesis optional around regexp
OPTIONAL_PARS = lambda R: OR(S(r'\(\s*', R, r'\s*\)'), R)
def test_optional_pars():
    r = OPTIONAL_PARS('abc')+'$'
    assert re.match(r, 'abc')
    assert re.match(r, '(abc)')
    assert not re.match(r, '(abcd)')
    assert not re.match(r, '(abc')
    assert not re.match(r, 'abc)')


# this disables the MULTILINE flag, so it will match at the
# beginning of the file:
RE_FILE_BEGIN = r'(?-m:^)'

# C primitives:

SP = r'\s*'

RE_COMMENT = r'//[^\n]*$|/\*([^*]|\*[^/])*\*/'
RE_COMMENTS = M(RE_COMMENT + SP)

RE_IDENTIFIER = r'[a-zA-Z_][a-zA-Z0-9_]*(?![a-zA-Z0-9])'
RE_STRING = r'"([^"\\]|\\[a-z"])*"'
RE_NUMBER = r'[0-9]+|0x[0-9a-fA-F]+'

# space or escaped newlines:
CPP_SPACE = OR(r'\s', r'\\\n', repeat='+')

RE_PATH = '[a-zA-Z0-9/_.-]+'

RE_INCLUDEPATH = OR(S(r'\"', RE_PATH, r'\"'),
                    S(r'<', RE_PATH, r'>'))

RE_INCLUDE = S(r'^[ \t]*#[ \t]*include[ \t]+', NAMED('includepath', RE_INCLUDEPATH), r'[ \t]*\n')
RE_SIMPLEDEFINE = S(r'^[ \t]*#[ \t]*define[ \t]+', RE_IDENTIFIER, r'[ \t]*\n')

RE_STRUCT_TYPE = S(r'struct\s+', RE_IDENTIFIER)
RE_TYPE = OR(RE_IDENTIFIER, RE_STRUCT_TYPE)
