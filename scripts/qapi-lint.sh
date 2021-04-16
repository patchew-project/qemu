#!/usr/bin/env bash
set -e

if [[ -f qapi/.flake8 ]]; then
    echo "flake8 --config=qapi/.flake8 qapi/"
    flake8 --config=qapi/.flake8 qapi/
fi
if [[ -f qapi/pylintrc ]]; then
    echo "pylint --rcfile=qapi/pylintrc qapi/"
    pylint --rcfile=qapi/pylintrc qapi/
fi
if [[ -f qapi/mypy.ini ]]; then
    echo "mypy --config-file=qapi/mypy.ini qapi/"
    mypy --config-file=qapi/mypy.ini qapi/
fi

if [[ -f qapi/.isort.cfg ]]; then
    pushd qapi
    echo "isort -c ."
    isort -c .
    popd
fi

pushd ../bin/git
make -j9
make check-qapi-schema
make docs
make sphinxdocs
popd
