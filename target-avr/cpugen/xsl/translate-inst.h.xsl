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

    <xsl:include href="utils.xsl"/>

<xsl:strip-space elements="*"/>
<xsl:output method="text" omit-xml-declaration="yes" indent="yes"/>

<xsl:template match="/cpu/instructions">
        <xsl:value-of select="$license" />
<xsl:text>
#ifndef AVR_TRANSLATE_INST_H_
#define AVR_TRANSLATE_INST_H_

typedef struct DisasContext    DisasContext;

</xsl:text>

<xsl:apply-templates select="//instruction" />

<xsl:text>
#endif
</xsl:text>
</xsl:template>

<xsl:template match="instruction">
    <xsl:variable name="length"     select="string-length(@opcode)" />
    <xsl:variable name="datatype"   select="mine:get-opcode-struct-type($length)" />
    <xsl:variable name="namestem"   select="@name"/>
    <xsl:variable name="ilength"    select="@length"/>

    <xsl:value-of select="concat('int ', /cpu/@name, '_translate_', @name, '(CPUAVRState *env, DisasContext* ctx, uint32_t opcode);', $newline)" />
    <xsl:for-each select="fields/field">
        <xsl:sort select="position()" data-type="number" order="descending"/>
        <xsl:choose>
            <xsl:when test="str:replace(str:replace(@name, '0', ''), '1', '') = ''">
            </xsl:when>
            <xsl:otherwise>
                <xsl:variable name="field" select="substring(@name, 2, string-length(@name) - 1)" />
                <xsl:variable name="h"     select="../field[@name = concat('h', $field)]"/>
                <xsl:variable name="l"     select="../field[@name = concat('l', $field)]"/>
                <xsl:variable name="m"     select="../field[@name = concat('m', $field)]"/>

                <xsl:if test="not($h/@name = @name or $m/@name = @name or $l/@name = @name)">
                    <xsl:value-of select="concat('static inline uint32_t ', $namestem, '_', @name, '(uint32_t opcode)', $newline)" />
                    <xsl:value-of select="concat('{', $newline)" />
                    <xsl:value-of select="concat('    return extract32(opcode, ', $ilength - @offset - @length, ', ', @length, ');', $newline)" />
                    <xsl:value-of select="concat('}', $newline)" />
                </xsl:if>

                <xsl:if test="$l and $h and ($h/@name = @name)">
                    <xsl:value-of select="concat('static inline uint32_t ', $namestem, '_', $field, '(uint32_t opcode)', $newline)" />
                    <xsl:value-of select="concat('{', $newline)" />
                    <xsl:value-of select="'    return '" />

<xsl:variable name="l_length"   select="$l/@length"/>
<xsl:variable name="l_offset"   select="$ilength - $l/@offset - $l_length"/>

<xsl:variable name="m_length"   select="$m/@length"/>
<xsl:variable name="m_offset"   select="$ilength - $m/@offset - $m_length"/>

<xsl:variable name="h_length"   select="$h/@length"/>
<xsl:variable name="h_offset"   select="$ilength - $h/@offset - $h_length"/>


                    <xsl:choose>
                        <xsl:when test="$h and $m and $l">
                            <xsl:value-of select="concat('(extract32(opcode, ', $h_offset, ', ', $h_length, ') &lt;&lt; ', $l_length + $m_length, ') | ')" />
                            <xsl:value-of select="concat('(extract32(opcode, ', $m_offset, ', ', $m_length, ') &lt;&lt; ', $l_length, ') | ')" />
                            <xsl:value-of select="concat('(extract32(opcode, ', $l_offset, ', ', $l_length, '))')" />
                        </xsl:when>
                        <xsl:otherwise>
                            <xsl:value-of select="concat('(extract32(opcode, ', $h_offset, ', ', $h_length, ') &lt;&lt; ', $l_length, ') | ')" />
                            <xsl:value-of select="concat('(extract32(opcode, ', $l_offset, ', ', $l_length, '))')" />
                        </xsl:otherwise>
                    </xsl:choose>

                    <xsl:value-of select="concat(';', $newline)"/>
                    <xsl:value-of select="concat('}', $newline)"/>
                </xsl:if>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:for-each>
    <xsl:value-of select="$newline" />
</xsl:template>

</xsl:stylesheet>
