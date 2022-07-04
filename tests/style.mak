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

prohibit_doubled_words_ = \
    the then in an on if is it but for or at and do to can
# expand the regex before running the check to avoid using expensive captures
prohibit_doubled_word_expanded_ = \
    $(join $(prohibit_doubled_words_),$(addprefix \s+,$(prohibit_doubled_words_)))
prohibit_doubled_word_RE_ ?= \
    /\b(?:$(subst $(_sp),|,$(prohibit_doubled_word_expanded_)))\b/gims
prohibit_doubled_word_ =						\
    -e 'while ($(prohibit_doubled_word_RE_))'				\
    $(perl_filename_lineno_text_)

# Define this to a regular expression that matches
# any filename:dd:match lines you want to ignore.
# The default is to ignore no matches.
ignore_doubled_word_match_RE_ ?= ^$$

sc_prohibit_doubled_word:
	@$(VC_LIST_EXCEPT)						\
	  | xargs perl -n -0777 $(prohibit_doubled_word_)		\
	  | $(GREP) -vE '$(ignore_doubled_word_match_RE_)'		\
	  | $(GREP) .							\
	  && { echo '$(ME): doubled words' 1>&2; exit 1; }		\
	  || :
