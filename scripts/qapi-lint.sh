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

if [[ -f ../docs/sphinx/qapi-domain.py ]]; then
    pushd ../docs/sphinx

    echo "mypy --strict qapi-domain.py"
    mypy --strict qapi-domain.py
    echo "flake8 qapi-domain.py --max-line-length=99"
    flake8 qapi-domain.py --max-line-length=99
    echo "isort qapi-domain.py"
    isort qapi-domain.py
    echo "black --check qapi-domain.py"
    black --check qapi-domain.py

    popd
fi

if [[ -f ../docs/sphinx/qapidoc.py ]]; then
    pushd ../docs/sphinx

    echo "pylint --rc-file ../../scripts/qapi/pylintrc qapidoc.py"
    PYTHONPATH=../scripts/ pylint \
                         --rc-file ../../scripts/qapi/pylintrc \
                         qapidoc.py
    echo "flake8 qapidoc.py --max-line-length=80"
    flake8 qapidoc.py --max-line-length=80
    echo "isort qapidoc.py"
    isort qapidoc.py
    echo "black --line-length 80 --check qapidoc.py"
    black --line-length 80 --check qapidoc.py

    popd
fi

pushd ../build
make -j13
make check-qapi-schema
make docs
make sphinxdocs
popd
