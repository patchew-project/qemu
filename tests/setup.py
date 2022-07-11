# This setup file is just-enough-config to allow pip to bootstrap a
# testing environment. It is not meant to be executed directly.
# See also: setup.cfg

import setuptools
import pkg_resources


def main():
    # https://medium.com/@daveshawley/safely-using-setup-cfg-for-metadata-1babbe54c108
    pkg_resources.require('setuptools>=39.2')
    setuptools.setup()


if __name__ == '__main__':
    main()
