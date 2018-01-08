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

# Wikipedia languages.
languages = [
  "en", "da", "sv", "no", "de", "fr", "es", "it", "nl", "pt", "pl", "fi"
]

# Command-line flags.
flags.define("--wikidata",
             help="wikidata version",
             default="20161031",
             metavar="YYYYMMDD")
flags.define("--wikipedia",
             help="wikipedia version",
             default="20161101",
             metavar="YYYYMMDD")

def wikidata_dump():
  """WikiData dump location."""
  return flags.arg.corpora + "/wikidata/wikidata-" + \
         flags.arg.wikidata + "-all.json.bz2"

def wikipedia_dump(language=None):
  """Wikipedia dump location."""
  if language == None: language = flags.arg.language
  return flags.arg.corpora + "/wikipedia/" + language + "wiki-" + \
         flags.arg.wikipedia + "-pages-articles.xml.bz2"

def wikidir():
  """Location of wiki datasets."""
  return flags.arg.workdir + "/wiki"

