#!/usr/bin/env python

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Lints all of Cobalt's C++ files."""

from __future__ import print_function

import os
import shutil
import subprocess
import sys
import re

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir, 'out'))
CLANG_TIDY = os.path.join(SRC_ROOT_DIR, 'sysroot', 'share', 'clang',
                          'run-clang-tidy.py')
CHECK_HEADER_GUARDS = os.path.join(SRC_ROOT_DIR, 'tools', 'style',
                                   'check-header-guards.py')

# A list of directories which should be skipped while walking the directory
# tree looking for C++ files to be linted. We also skip directories starting
# with a "." such as ".git"
SKIP_LINT_DIRS = [
    os.path.join(SRC_ROOT_DIR, 'out'),
    os.path.join(SRC_ROOT_DIR, 'sysroot'),
    os.path.join(SRC_ROOT_DIR, 'third_party'),
    os.path.join(SRC_ROOT_DIR, 'src', 'bin', 'config_parser', 'src',
                 'source_generator', 'source_generator_test_files'),
]

TEST_FILE_REGEX = re.compile('.*_(unit)?tests?.cc$')
TEST_FILE_CLANG_TIDY_CHECKS = [
    '-readability-magic-numbers',
    '-misc-non-private-member-variables-in-classes'
]


# Given a directory's parent path and name, returns a boolean indicating whether
# or not the directory should be skipped for liniting.
def should_skip_dir(parent_path, name):
  if name.startswith('.'):
    return True
  full_path = os.path.join(parent_path, name)
  for p in SKIP_LINT_DIRS:
    if full_path.startswith(p):
      return True
  return False


def main(only_directories=[]):
  status = 0

  clang_tidy_files = []
  clang_tidy_test_files = []

  only_directories = [os.path.join(SRC_ROOT_DIR, d) for d in only_directories]

  for root, dirs, files in os.walk(SRC_ROOT_DIR):
    for f in files:
      if f.endswith('.h') or f.endswith('.cc'):
        full_path = os.path.join(root, f)

        if only_directories and len(
            [d for d in only_directories if full_path.startswith(d)]) == 0:
          continue

        if TEST_FILE_REGEX.match(full_path):
          clang_tidy_test_files.append(full_path)
        else:
          clang_tidy_files.append(full_path)

    # Before recursing into directories remove the ones we want to skip.
    dirs_to_skip = [dir for dir in dirs if should_skip_dir(root, dir)]
    for d in dirs_to_skip:
      dirs.remove(d)

  header_files = [
      f for f in clang_tidy_files + clang_tidy_test_files
      if f.endswith('.h') and not os.path.islink(f)
  ]
  print('Running check-header-guards.py on %d files' % len(header_files))
  if len(header_files) > 0:
    try:
      subprocess.check_call([CHECK_HEADER_GUARDS] + header_files)
    except:
      status += 1

  clang_tidy_command = [CLANG_TIDY, '-quiet', '-p', OUT_DIR]
  print('Running clang-tidy on %d source files' % len(clang_tidy_files))
  if len(clang_tidy_files) > 0:
    try:
      subprocess.check_call(clang_tidy_command + clang_tidy_files)
    except:
      status += 1

  print('Running clang-tidy on %d test files' % len(clang_tidy_test_files))
  if len(clang_tidy_test_files) > 0:
    try:
      subprocess.check_call(
          clang_tidy_command +
          ['-checks=%s' % ','.join(TEST_FILE_CLANG_TIDY_CHECKS)] +
          clang_tidy_test_files)
    except:
      status += 1

  return status


if __name__ == '__main__':
  exit(main())
