import subprocess


def _external_test(command_line: str) -> None:
    args = command_line.split(' ')
    try:
        subprocess.run(args, check=True)
    except subprocess.CalledProcessError as err:
        # Re-contextualize to avoid pytest showing error context inside
        # the subprocess module itself
        raise Exception("'{:s}' returned {:d}".format(
            ' '.join(args), err.returncode)) from None


def test_isort() -> None:
    _external_test('isort -c qemu')


def test_flake8() -> None:
    _external_test('flake8')


def test_pylint() -> None:
    _external_test('pylint qemu')


def test_mypy() -> None:
    _external_test('mypy -p qemu')
