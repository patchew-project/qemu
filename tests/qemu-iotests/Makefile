PYMODULES = $(wildcard *.py)

KNOWN_GOOD = iotests.py nbd-fault-injector.py

CLEANFILES= *.out.bad *.notrun check.log check.time*

# no default target
default:

clean:
	rm -f $(CLEANFILES)

delint:
	pylint $(KNOWN_GOOD)
	pylint --disable=R,C,W $(filter-out $(KNOWN_GOOD), $(PYMODULES))
