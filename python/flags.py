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
args = None

# Initialize command-line argument parser.
parser = argparse.ArgumentParser(
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)

def arg(*args, **kwargs):
  """Define command-line flag."""
  parser.add_argument(*args, **kwargs)

def parse():
  """Parse command-line flags."""
  global args
  args = parser.parse_args()
  if args.corpora == None: args.corpora = args.data + "/corpora"
  if args.repository == None: args.repository = args.data + "/sling"

# Standard command-line flags.
arg("--language",
    help="primary language for resources",
    default="en",
    metavar="LANG")

arg("--data",
    help="data directory",
    default="/var/data",
    metavar="DIR")

arg("--corpora",
    help="corpus directory",
    metavar="DIR")

arg("--repository",
    help="SLING git repository directory",
    metavar="DIR")

