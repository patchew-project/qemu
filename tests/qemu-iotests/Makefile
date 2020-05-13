PYMODULES = $(wildcard *.py)

CLEANFILES= *.out.bad *.notrun check.log check.time*

# no default target
default:

clean:
	rm -f $(CLEANFILES)

delint:
	pylint $(PYMODULES)
