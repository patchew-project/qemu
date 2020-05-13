PYMODULES = $(wildcard *.py)
PYTESTS := $(shell grep -rl '^\#!/usr/bin/env python3' *)
PYTESTS := $(filter-out $(PYMODULES), $(PYTESTS))

CLEANFILES= *.out.bad *.notrun check.log check.time*

# no default target
default:

clean:
	rm -f $(CLEANFILES)

delint:
	pylint $(PYMODULES)
	pylint --disable=R,C,W $(PYTESTS)
