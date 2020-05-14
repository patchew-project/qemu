#!/usr/bin/env3 python
"""
QEMU tooling installer script
Copyright (c) 2020 John Snow for Red Hat, Inc.
"""

import setuptools

def main():
    """
    QEMU tooling installer
    """

    kwargs = {
        'name': 'qemu',
        'use_scm_version': {
            'root': '..',
            'relative_to': __file__,
        },
        'maintainer': 'QEMU Developer Team',
        'maintainer_email': 'qemu-devel@nongnu.org',
        'url': 'https://www.qemu.org/',
        'download_url': 'https://www.qemu.org/download/',
        'packages': setuptools.find_namespace_packages(),
        'description': 'QEMU Python Build, Debug and SDK tooling.',
        'classifiers': [
            'Development Status :: 5 - Production/Stable',
            'License :: OSI Approved :: GNU General Public License v2 (GPLv2)',
            'Natural Language :: English',
            'Operating System :: OS Independent',
        ],
        'platforms': [],
        'keywords': [],
        'setup_requires': [
            'setuptools',
            'setuptools_scm',
        ],
        'install_requires': [
        ],
        'python_requires': '>=3.6',
        'long_description_content_type': 'text/x-rst',
    }

    with open("README.rst", "r") as fh:
        kwargs['long_description'] = fh.read()

    setuptools.setup(**kwargs)

if __name__ == '__main__':
    main()
