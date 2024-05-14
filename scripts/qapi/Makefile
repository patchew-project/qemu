check:
	isort -c .
	flake8 .
	cd .. && pylint --rcfile=qapi/pylintrc qapi
	cd .. && mypy -p qapi --config-file=qapi/mypy.ini
