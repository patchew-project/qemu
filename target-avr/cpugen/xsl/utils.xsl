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
    xmlns:str   = "http://exslt.org/strings"
    xmlns:func  = "http://exslt.org/functions"
    xmlns:exsl  = "http://exslt.org/common"
    xmlns:mine  = "mrolnik@gmail.com"
    extension-element-prefixes="func">

    <xsl:output method = "text" />
    <xsl:strip-space elements="*"/>

    <xsl:variable name="newline">
    <xsl:text>
</xsl:text>
    </xsl:variable>
    <xsl:variable name="license">
        <xsl:text>/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * &lt;http://www.gnu.org/licenses/lgpl-2.1.html&gt;
 */

</xsl:text>
    </xsl:variable>

    <xsl:variable name="tab">
        <xsl:text>    </xsl:text>
    </xsl:variable>

    <func:function name="mine:pad" as="string">
        <xsl:param      name="str"      as="xs:string"/>
        <xsl:param      name="len"      as="xs:integer"/>
        <xsl:variable   name="lstr"     select="string-length($str)"/>
        <xsl:variable   name="pad"      select="str:padding($len - $lstr, ' ')"/>
        <func:result select="concat($str,$pad)"/>
    </func:function>

    <func:function  name="mine:toupper" as="string">
        <xsl:param      name="str"      as="xs:string"/>
        <xsl:variable   name="upper"    select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ ,-.'" />
        <xsl:variable   name="lower"    select="'abcdefghijklmnopqrstuvwxyz____'" />
        <func:result    select="translate($str, $upper, $lower)" />
    </func:function>

    <func:function  name="mine:toname" as="string">
        <xsl:param      name="str"      as="xs:string"/>
        <xsl:variable   name="src"      select="'. ,-'" />
        <xsl:variable   name="dst"      select="'_'" />
        <func:result    select="translate($str, $src, $dst)" />
    </func:function>

    <func:function name="mine:get-opcode-struct-type">
        <xsl:param name="length"/>
            <xsl:choose>
                <xsl:when test="$length &lt; '9'">
                    <func:result select="'uint8_t '"/>
                </xsl:when>
                <xsl:when test="$length &lt; '17'">
                    <func:result select="'uint16_t'"/>
                </xsl:when>
                <xsl:when test="$length &lt; '33'">
                    <func:result select="'uint32_t'"/>
                </xsl:when>
                <xsl:when test="$length &lt; '65'">
                    <func:result select="'uint64_t'"/>
                </xsl:when>
            </xsl:choose>
    </func:function>

</xsl:stylesheet>
