# QEMU Monitor Protocol Lexer Extension
#
# Copyright (C) 2019, Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#  John Snow <jsnow@redhat.com>
#
# This work is licensed under the terms of the GNU GPLv2 or later.
# See the COPYING file in the top-level directory.
"""qmp_lexer is a Sphinx extension that provides a QMP lexer for code blocks."""

from pygments.lexer import RegexLexer, DelegatingLexer
from pygments.lexers.data import JsonLexer
import pygments.token

class QMPExampleMarkersLexer(RegexLexer):
    """QMPExampleMarkersLexer lexes directionality flow indicators."""
    tokens = {
        'root': [
            (r'-> ', pygments.token.Generic.Prompt),
            (r'<- ', pygments.token.Generic.Prompt),
        ]
    }

class QMPExampleLexer(DelegatingLexer):
    """QMPExampleLexer lexes annotated QMP examples."""
    def __init__(self, **options):
        super(QMPExampleLexer, self).__init__(JsonLexer, QMPExampleMarkersLexer,
                                              pygments.token.Error, **options)

def setup(sphinx):
    """For use by the Sphinx extensions API."""
    sphinx.add_lexer("QMP", QMPExampleLexer())
