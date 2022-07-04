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

ME := tests/style.mak

srcdir = .

AWK ?= awk
GREP ?= grep
SED ?= sed

# Helper variables.
_empty =
_sp = $(_empty) $(_empty)

VC_LIST = (cd $(srcdir) && git ls-tree -r 'HEAD:' | \
	sed -n "s|^100[^	]*.||p" )

# You can override this variable in cfg.mk if your gnulib submodule lives
# in a different location.
gnulib_dir ?= $(shell if test -f $(srcdir)/gnulib/gnulib-tool; then \
			echo $(srcdir)/gnulib; \
		else \
			echo ${GNULIB_SRCDIR}; \
		fi)

# You can override this variable in cfg.mk to set your own regexp
# matching files to ignore.
VC_LIST_ALWAYS_EXCLUDE_REGEX ?= ^$$

# This is to preprocess robustly the output of $(VC_LIST), so that even
# when $(srcdir) is a pathological name like "....", the leading sed command
# removes only the intended prefix.
_dot_escaped_srcdir = $(subst .,\.,$(srcdir))

# Post-process $(VC_LIST) output, prepending $(srcdir)/, but only
# when $(srcdir) is not ".".
ifeq ($(srcdir),.)
  _prepend_srcdir_prefix =
else
  _prepend_srcdir_prefix = | $(SED) 's|^|$(srcdir)/|'
endif

# In order to be able to consistently filter "."-relative names,
# (i.e., with no $(srcdir) prefix), this definition is careful to
# remove any $(srcdir) prefix, and to restore what it removes.
_sc_excl = \
  $(or $(subst $(_sp),|,$(exclude_file_name_regexp--$@)),^$$)

VC_LIST_EXCEPT = \
  $(VC_LIST) | $(SED) 's|^$(_dot_escaped_srcdir)/||' \
	| if test -f $(srcdir)/.x-$@; then $(GREP) -vEf $(srcdir)/.x-$@; \
	  else $(GREP) -Ev -e "$${VC_LIST_EXCEPT_DEFAULT-ChangeLog}"; fi \
	| $(GREP) -Ev -e '($(VC_LIST_ALWAYS_EXCLUDE_REGEX)|$(_sc_excl))' \
	$(_prepend_srcdir_prefix)


# Prevent programs like 'sort' from considering distinct strings to be equal.
# Doing it here saves us from having to set LC_ALL elsewhere in this file.
export LC_ALL = C

# Collect the names of rules starting with 'sc_'.
syntax-check-rules := $(sort $(shell env LC_ALL=C $(SED) -n \
   's/^\(sc_[a-zA-Z0-9_-]*\):.*/\1/p' $(srcdir)/$(ME)))
.PHONY: $(syntax-check-rules)

ifeq ($(shell $(VC_LIST) >/dev/null 2>&1; echo $$?),0)
  local-checks-available += $(syntax-check-rules)
else
  local-checks-available += no-vc-detected
no-vc-detected:
	@echo "No version control files detected; skipping syntax check"
endif
.PHONY: $(local-checks-available)

# Arrange to print the name of each syntax-checking rule just before running it.
$(syntax-check-rules): %: %.m
sc_m_rules_ = $(patsubst %, %.m, $(syntax-check-rules))
.PHONY: $(sc_m_rules_)
$(sc_m_rules_):
	@echo $(patsubst sc_%.m, %, $@)
	@date +%s.%N > .sc-start-$(basename $@)

# Compute and print the elapsed time for each syntax-check rule.
sc_z_rules_ = $(patsubst %, %.z, $(syntax-check-rules))
.PHONY: $(sc_z_rules_)
$(sc_z_rules_): %.z: %
	@end=$$(date +%s.%N);						\
	start=$$(cat .sc-start-$*);					\
	rm -f .sc-start-$*;						\
	$(AWK) -v s=$$start -v e=$$end					\
	  'END {printf "%.2f $(patsubst sc_%,%,$*)\n", e - s}' < /dev/null

# The patsubst here is to replace each sc_% rule with its sc_%.z wrapper
# that computes and prints elapsed time.
local-check :=								\
  $(patsubst sc_%, sc_%.z,						\
    $(filter-out $(local-checks-to-skip), $(local-checks-available)))

syntax-check: $(local-check)

.DEFAULT_GOAL := syntax-check

# _sc_search_regexp
#
# This macro searches for a given construct in the selected files and
# then takes some action.
#
# Parameters (shell variables):
#
#  prohibit | require
#
#     Regular expression (ERE) denoting either a forbidden construct
#     or a required construct.  Those arguments are exclusive.
#
#  exclude
#
#     Regular expression (ERE) denoting lines to ignore that matched
#     a prohibit construct.  For example, this can be used to exclude
#     comments that mention why the nearby code uses an alternative
#     construct instead of the simpler prohibited construct.
#
#  in_vc_files | in_files
#
#     grep-E-style regexp selecting the files to check.  For in_vc_files,
#     the regexp is used to select matching files from the list of all
#     version-controlled files; for in_files, it's from the names printed
#     by "find $(srcdir)".  When neither is specified, use all files that
#     are under version control.
#
#  containing | non_containing
#
#     Select the files (non) containing strings matching this regexp.
#     If both arguments are specified then CONTAINING takes
#     precedence.
#
#  with_grep_options
#
#     Extra options for grep.
#
#  ignore_case
#
#     Ignore case.
#
#  halt
#
#     Message to display before to halting execution.
#
# Finally, you may exempt files based on an ERE matching file names.
# For example, to exempt from the sc_space_tab check all files with the
# .diff suffix, set this Make variable:
#
# exclude_file_name_regexp--sc_space_tab = \.diff$
#
# Note that while this functionality is mostly inherited via VC_LIST_EXCEPT,
# when filtering by name via in_files, we explicitly filter out matching
# names here as well.

# Initialize each, so that envvar settings cannot interfere.
export require =
export prohibit =
export exclude =
export in_vc_files =
export in_files =
export containing =
export non_containing =
export halt =
export with_grep_options =

# By default, _sc_search_regexp does not ignore case.
export ignore_case =
_ignore_case = $$(test -n "$$ignore_case" && printf %s -i || :)

define _sc_say_and_exit
   dummy=; : so we do not need a semicolon before each use;		\
   { printf '%s\n' "$(ME): $$msg" 1>&2; exit 1; };
endef

define _sc_search_regexp
   dummy=; : so we do not need a semicolon before each use;		\
									\
   : Check arguments;							\
   test -n "$$prohibit" && test -n "$$require"				\
     && { msg='Cannot specify both prohibit and require'		\
          $(_sc_say_and_exit) } || :;					\
   test -z "$$prohibit" && test -z "$$require"				\
     && { msg='Should specify either prohibit or require'		\
          $(_sc_say_and_exit) } || :;					\
   test -z "$$prohibit" && test -n "$$exclude"				\
     && { msg='Use of exclude requires a prohibit pattern'		\
          $(_sc_say_and_exit) } || :;					\
   test -n "$$in_vc_files" && test -n "$$in_files"			\
     && { msg='Cannot specify both in_vc_files and in_files'		\
          $(_sc_say_and_exit) } || :;					\
   test "x$$halt" != x							\
     || { msg='halt not defined' $(_sc_say_and_exit) };			\
									\
   : Filter by file name;						\
   if test -n "$$in_files"; then					\
     files=$$(find $(srcdir) | $(GREP) -E "$$in_files"			\
              | $(GREP) -Ev '$(_sc_excl)');				\
   else									\
     files=$$($(VC_LIST_EXCEPT));					\
     if test -n "$$in_vc_files"; then					\
       files=$$(echo "$$files" | $(GREP) -E "$$in_vc_files");		\
     fi;								\
   fi;									\
									\
   : Filter by content;							\
   test -n "$$files"							\
     && test -n "$$containing"						\
     && { files=$$(echo "$$files" | xargs $(GREP) -l "$$containing"); }	\
     || :;								\
   test -n "$$files"							\
     && test -n "$$non_containing"					\
     && { files=$$(echo "$$files" | xargs $(GREP) -vl "$$non_containing"); } \
     || :;								\
									\
   : Check for the construct;						\
   if test -n "$$files"; then						\
     if test -n "$$prohibit"; then					\
       echo "$$files"							\
         | xargs $(GREP) $$with_grep_options $(_ignore_case) -nE	\
		"$$prohibit" /dev/null					\
         | $(GREP) -vE "$${exclude:-^$$}"				\
         && { msg="$$halt" $(_sc_say_and_exit) }			\
         || :;								\
     else								\
       echo "$$files"							\
         | xargs							\
             $(GREP) $$with_grep_options $(_ignore_case) -LE "$$require" \
         | $(GREP) .							\
         && { msg="$$halt" $(_sc_say_and_exit) }			\
         || :;								\
     fi									\
   else :;								\
   fi || :;
endef

# Perl block to convert a match to FILE_NAME:LINENO:TEST
perl_filename_lineno_text_ =						\
    -e '  {'								\
    -e '    $$n = ($$` =~ tr/\n/\n/ + 1);'				\
    -e '    ($$v = $$&) =~ s/\n/\\n/g;'					\
    -e '    print "$$ARGV:$$n:$$v\n";'					\
    -e '  }'
