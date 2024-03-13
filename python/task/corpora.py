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

import os
import sling.flags as flags

# Command-line flags.
flags.define("--language",
             help="primary language for resources",
             default="en",
             metavar="LANG")

flags.define("--languages",
             help="list of languages to process",
             metavar="LANG,...")

flags.define("--wikidata",
             help="wikidata version",
             default="latest",
             metavar="YYYYMMDD")

flags.define("--wikipedia",
             help="wikipedia version",
             default="latest",
             metavar="YYYYMMDD")

flags.define("--overwrite",
             help="overwrite existing file",
             default=False,
             action='store_true')

flags.define("--wikidatadb",
             help="wikidata database",
             default="wikidata",
             metavar="DBURL")

flags.define("--kbdb",
             help="knowledge base database",
             default="kb",
             metavar="DBURL")

flags.define("--photodb",
             help="photo database",
             default="photo",
             metavar="DBURL")

flags.define("--imdb",
             help="IMDB profile database",
             default="imdb",
             metavar="DBURL")

def post_process_flags(arg):
  if arg.languages == None:
    arg.languages = arg.language
  if arg.languages == "ALL":
    arg.languages = "en,da,sv,no,de,fr,es,it,nl,pt,pl,fi"
  arg.languages = arg.languages.split(",")

flags.hook(post_process_flags)

def corpusdir(subdir):
  """Location of corpora."""
  return flags.arg.corpora + "/" + subdir

def workdir(subdir, language=None):
  """Location of datasets."""
  if language == None:
    return flags.arg.workdir + "/" + subdir
  else:
    return flags.arg.workdir + "/" + subdir + "/" + language

def wikidata_url():
  """URL for downloading Wikidata dump."""
  mid = ""
  if flags.arg.wikidata != "latest":
    mid = flags.arg.wikidata + "/wikidata-"
  return "https://dumps.wikimedia.org/wikidatawiki/entities/" + \
          mid + flags.arg.wikidata + "-all.json.bz2"

def wikidata_dump():
  """WikiData dump location."""
  return corpusdir("wikidata") + "/wikidata-" + \
         flags.arg.wikidata + "-all.json.bz2"

def wikipedia_url(language=None):
  """URL for downloading Wikipedia dump."""
  if language == None: language = flags.arg.language
  return "https://dumps.wikimedia.org/" + language + "wiki/" + \
         flags.arg.wikipedia + "/" + language + "wiki-" + \
         flags.arg.wikipedia + "-pages-articles.xml.bz2"

def wikipedia_dump(language=None):
  """Wikipedia dump location."""
  if language == None: language = flags.arg.language
  return corpusdir("wikipedia") + "/" + language + "wiki-" + \
         flags.arg.wikipedia + "-pages-articles.xml.bz2"

def wikidatadb():
  """WikiData database."""
  return flags.arg.wikidatadb

def kbdb():
  """Knowledge base database."""
  return flags.arg.kbdb

def photodb():
  """Photo database."""
  return flags.arg.photodb

def imdb():
  """IMDB profile database."""
  return flags.arg.imdb

def wikidir(language=None):
  """Location of wiki datasets."""
  return workdir("wiki", language)

def kbdir(language=None):
  """Location of knowledge base datasets."""
  return workdir("kb", language)

def repository(path):
  """Location of file in Git repository."""
  return flags.arg.repository + "/" + path

