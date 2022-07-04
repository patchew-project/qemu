# -*- makefile -*-
#
# Rules for simple code style checks applying across the
# whole tree. Partially derived from GNULIB's 'maint.mk'
#
# Copyright (C) 2008-2019 Red Hat, Inc.
# Copyright (C) 2003-2019 Free Software Foundation, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <http://www.gnu.org/licenses/>.

include tests/style-infra.mak
include tests/style-excludes.mak

# Use 'bool', not 'int', when assigning true or false
sc_int_assign_bool:
	@prohibit='\<int\>.*= *(true|false)\b' \
	halt='use bool type for boolean values' \
	$(_sc_search_regexp)
