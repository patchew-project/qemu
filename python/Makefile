.PHONY: help venv venv-check check clean distclean

help:
	@echo "python packaging help:"
	@echo ""
	@echo "make venv:       Create pipenv's virtual environment."
	@echo "    NOTE: Requires Python 3.6 and pipenv."
	@echo "          Will download packages from PyPI."
	@echo "    HINT: On Fedora: 'sudo dnf install python36 pipenv'"
	@echo ""
	@echo "make venv-check: run linters using pipenv's virtual environment."
	@echo ""
	@echo "make check:      run linters using the current environment."
	@echo "    Hint: Install deps with: 'pip install \".[devel]\"'"
	@echo ""
	@echo "make clean:      remove build output."
	@echo ""
	@echo "make distclean:  remove venv files and everything from 'clean'."


venv: .venv
.venv: Pipfile.lock
	@PIPENV_VENV_IN_PROJECT=1 pipenv sync --dev --keep-outdated

venv-check: venv
	@pipenv run make check

check:
	@pytest

clean:
	rm -rf build/ dist/

distclean: clean
	rm -rf qemu.egg.info/ .venv
