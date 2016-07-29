<?xml version="1.0"?>
<!--
   CPUGEN

   Copyright (c) 2016 Michael Rolnik

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
-->

<xsl:stylesheet version="1.0"
    xmlns:xsl   = "http://www.w3.org/1999/XSL/Transform"
    xmlns:func  = "http://exslt.org/functions"
    xmlns:str   = "http://exslt.org/strings"
    xmlns:mine  = "mrolnik@gmail.com"
    extension-element-prefixes="func"
    >

    <xsl:strip-space elements="*"/>
    <xsl:output method="text" omit-xml-declaration="yes" indent="yes"/>

    <xsl:include href="utils.xsl"/>

    <xsl:template match="/cpu/instructions">
        <xsl:value-of select="$license" />
        <xsl:text>
#include &lt;stdint.h&gt;
#include "translate.h"

void </xsl:text><xsl:value-of select="/cpu/@name"/><xsl:text>_decode(uint32_t pc, uint32_t *l, uint32_t c, translate_function_t *t)
{
</xsl:text>
    <xsl:apply-templates select="switch">
        <xsl:with-param name="ident" ><xsl:value-of select="$tab"/></xsl:with-param>
    </xsl:apply-templates>
<xsl:text>
}
</xsl:text>
</xsl:template>

    <xsl:template match="switch">
        <xsl:param name="ident" />
        <xsl:if test="not (@bitoffset = ../../@bitoffset and @bits = ../../@bits)" >
            <xsl:value-of select="concat($ident, 'uint32_t opc  = extract32(c, ', @bitoffset, ', ', @bits, ');', $newline)" />
        </xsl:if>
        <xsl:value-of select="concat($ident, 'switch (opc &amp; ', @mask, ') {', $newline)"/>
        <xsl:apply-templates select="case">
            <xsl:with-param name="ident" ><xsl:value-of select="$ident"/><xsl:value-of select="$tab"/></xsl:with-param>
        </xsl:apply-templates>
        <xsl:value-of select="concat($ident, '}', $newline)"/>
    </xsl:template>

    <xsl:template match="case">
        <xsl:param name="ident" />

        <xsl:value-of select="concat($ident, 'case ', @value, ': {', $newline)"/>
        <xsl:apply-templates select="switch">
            <xsl:with-param name="ident"><xsl:value-of select="concat($ident, $tab)"/></xsl:with-param>
        </xsl:apply-templates>
        <xsl:apply-templates select="match01">
            <xsl:with-param name="ident"><xsl:value-of select="concat($ident, $tab)"/></xsl:with-param>
        </xsl:apply-templates>
        <xsl:apply-templates select="instruction">
            <xsl:with-param name="ident"><xsl:value-of select="concat($ident, $tab)"/></xsl:with-param>
        </xsl:apply-templates>
        <xsl:value-of select="concat($ident, $tab, 'break;', $newline)"/>
        <xsl:value-of select="concat($ident, '}', $newline)"/>
    </xsl:template>

    <xsl:template match="instruction">
        <xsl:param name="ident" />

        <xsl:value-of select="concat($ident, '*l = ', string-length(@opcode), ';', $newline)" />
        <xsl:value-of select="concat($ident, '*t = &amp;', /cpu/@name, '_translate_', @name, ';', $newline)" />
    </xsl:template>

    <xsl:template match="match01">
        <xsl:param name="ident" />

        <xsl:value-of select="concat($ident, 'if((opc &amp; ', @mask, ' == ', @value, ') {', $newline)" />
        <xsl:apply-templates select="instruction">
            <xsl:with-param name="ident"><xsl:value-of select="concat($ident, $tab)"/></xsl:with-param>
        </xsl:apply-templates>
        <xsl:value-of select="concat($ident, 'break;', $newline)"/>
        <xsl:value-of select="concat($ident, '}', $newline)"/>
    </xsl:template>

</xsl:stylesheet>
