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

"""Corpus locations"""

import sling.flags as flags

# Command-line flags.
flags.arg("--wikidata",
          help="wikidata version",
          default="20161031",
          metavar="YYYYMMDD")
flags.arg("--wikipedia",
          help="wikipedia version",
          default="20161101",
          metavar="YYYYMMDD")

def wikidata_dump():
  """WikiData dump location."""
  return flags.args.corpora + "/wikidata/wikidata-" + \
         flags.args.wikidata + "-all.json.bz2"

def wikipedia_dump(language=None):
  """Wikipedia dump location."""
  if language == None: language = flags.args.language
  return flags.args.corpora + "/wikipedia/" + language + "wiki-" + \
         flags.args.wikipedia + "-pages-articles.xml.bz2"

