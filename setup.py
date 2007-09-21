"""Distutils script for bsdiff.

Windows platforms:
    python setup.py build --compiler=mingw32 install

Unix platforms
    python setup.py build install

"""

import os
import sys

from distutils.core import setup
from distutils.extension import Extension
from distutils import sysconfig

# define the list of files to be included as documentation for Windows
dataFiles = None
if sys.platform in ("win32", "cygwin"):
    baseName = "cx_bsdiff-doc"
    dataFiles = [ (baseName, [ "LICENSE.TXT", "README.TXT" ]) ]
    htmlFiles = [os.path.join("html", n) for n in os.listdir("html") \
            if not n.startswith(".")]
    dataFiles.append( ("%s/%s" % (baseName, "html"), htmlFiles) )

# setup the extension
extension = Extension(
        name = "bsdiff",
        sources = ["bsdiff.c"])

# perform the setup
setup(
        name = "cx_bsdiff",
        version = "HEAD",
        data_files = dataFiles,
        description = "Python interface for bsdiff",
        license = "See LICENSE.txt",
        long_description = "Python interface for bsdiff",
        author = "Anthony Tuininga",
        author_email = "anthony.tuininga@gmail.com",
        url = "http://starship.python.net/crew/atuining",
        ext_modules = [extension])

