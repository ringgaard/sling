# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Command-line flags"""

import argparse

# Command line flag arguments.
arg = argparse.Namespace()

# Initialize command-line argument parser.
parser = argparse.ArgumentParser(
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)

def define(*args, **kwargs):
  """Define command-line flag."""
  parser.add_argument(*args, **kwargs)

def parse():
  """Parse command-line flags."""
  global arg
  parser.parse_args(namespace=arg)
  if arg.corpora == None: arg.corpora = arg.data + "/corpora"
  if arg.repository == None: arg.repository = arg.data + "/sling"
  if arg.workdir == None: arg.workdir = arg.data + "/e"

# Standard command-line flags.
define("--language",
       help="primary language for resources",
       default="en",
       metavar="LANG")

define("--data",
       help="data directory",
       default="/var/data",
       metavar="DIR")

define("--corpora",
       help="corpus directory",
       metavar="DIR")

define("--workdir",
       help="working directory",
       metavar="DIR")

define("--repository",
       help="SLING git repository directory",
       metavar="DIR")

