# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hashlib
import logging
import platform
import subprocess
import sys

# pylint: disable=E0611
from hashlib import sha256
# pylint: enable=E0611
from os.path import basename, realpath

_logging = logging.getLogger()

# pylint: disable=C0301
# Based on/taken from
#   http://code.activestate.com/recipes/578231-probably-the-fastest-memoization-decorator-in-the-/
# (with cosmetic changes).
# pylint: enable=C0301
def _memoize(f):
  """Memoization decorator for a function taking a single argument."""
  class Memoize(dict):
    def __missing__(self, key):
      rv = self[key] = f(key)
      return rv
  return Memoize().__getitem__

@_memoize
def _file_hash(filename):
  """Returns a string representing the hash of the given file."""
  _logging.debug("Hashing %s ...", filename)
  with open(filename, mode='rb') as f:
    m = hashlib.sha256()
    while True:
      block = f.read(4096)
      if not block:
        break
      m.update(block)
  _logging.debug("  => %s", m.hexdigest())
  return m.hexdigest()

@_memoize
def _get_dependencies(filename):
  """Returns a list of filenames for files that the given file depends on."""
  if platform.system() == 'Windows':
    # There's no ldd on Windows. We can try to bundle or require depends, but
    # given that we're not supporting component build this seems low priority.
    return []
  _logging.debug("Getting dependencies for %s ...", filename)
  lines = subprocess.check_output(['ldd', filename]).splitlines()
  rv = []
  for line in lines:
    i = line.find('/')
    if i < 0:
      _logging.debug("  => no file found in line: %s", line)
      continue
    rv.append(line[i:].split(None, 1)[0])
  _logging.debug("  => %s", rv)
  return rv

def transitive_hash(filename):
  """Returns a string that represents the "transitive" hash of the given
  file. The transitive hash is a hash of the file and all the shared libraries
  on which it depends (done in an order-independent way)."""
  hashes = set()
  to_hash = [filename]
  while to_hash:
    current_filename = realpath(to_hash.pop())
    current_hash = _file_hash(current_filename)
    if current_hash in hashes:
      _logging.debug("Already seen %s (%s) ...", current_filename, current_hash)
      continue
    _logging.debug("Haven't seen %s (%s) ...", current_filename, current_hash)
    hashes.add(current_hash)
    to_hash.extend(_get_dependencies(current_filename))
  return sha256('|'.join(sorted(hashes))).hexdigest()

def main(argv):
  logging.basicConfig()
  # Uncomment to debug:
  # _logging.setLevel(logging.DEBUG)

  if len(argv) < 2:
    print """\
Usage: %s [file] ...

Prints the \"transitive\" hash of each (executable) file. The transitive
hash is a hash of the file and all the shared libraries on which it
depends (done in an order-independent way).""" % basename(argv[0])
    return 0

  rv = 0
  for filename in argv[1:]:
    try:
      print transitive_hash(filename), filename
    except subprocess.CalledProcessError:
      print "ERROR", filename
      rv = 1
  return rv

if __name__ == '__main__':
  sys.exit(main(sys.argv))
