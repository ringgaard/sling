# Copyright 2021 Ringgaard Research ApS
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

"""Workflow for building alias tables."""

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task import *

class NamesWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def item_names(self, language=None):
    """Resource for item names in language. This is a set of record files with
    one SLING frame per item.
      <qid>: {
        alias: {
          name: "<alias>"
          lang: /lang/<lang>
          sources: ...
          count: ...
          form: ...
        }
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("names@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/alias")

  def alias_corrections(self):
    """Resource for alias corrections."""
    return self.wf.resource("aliases.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def extract_names(self, aliases=None, language=None):
    "Task for selecting language-dependent names for items."""
    if language == None: language = flags.arg.language

    if aliases == None:
      # Get language-dependent aliases from Wikidata and Wikpedia.
      wikidata_aliases = self.wf.map(self.data.fused_items(),
                                     "alias-extractor",
                                     params={
                                       "language": language,
                                       "skip_aux": True,
                                     },
                                     format="message/alias",
                                     name="wikidata-alias-extractor")
      wikipedia_aliases = self.wf.read(self.data.wikipedia_aliases(language),
                                       name="wikipedia-alias-reader")
      aliases = wikipedia_aliases + [wikidata_aliases]

    with self.wf.namespace("select"):
      # Group aliases on item id.
      output = self.item_names(language)
      num_shards = length_of(output)
      aliases_by_item = self.wf.shuffle(aliases, 10)

      # Filter and select aliases.
      selector = self.wf.task("alias-selector")
      selector.add_param("language", language)
      selector.add_param("anchor_threshold", 30)
      selector.add_param("min_prefix", 1)
      edit_distance = {
        "fi": 5,
        "pl": 5
      }.get(language, 3)
      selector.add_param("max_edit_distance", edit_distance)
      selector.attach_input("corrections", self.alias_corrections())
      self.wf.connect(aliases_by_item, selector)
      item_aliases = self.wf.channel(selector,
                                     shards=num_shards,
                                     format=format_of(output).as_message())

    with self.wf.namespace("merge"):
      # Group aliases by alias fingerprint.
      aliases_by_fp = self.wf.shuffle(item_aliases, num_shards)

      # Merge all aliases for fingerprint.
      merger = self.wf.reduce(aliases_by_fp, output, "alias-merger",
                              auxin={"kb": self.data.knowledge_base()})

    return output

  def build_name_table(self, names=None, language=None):
    """Build name table for all items."""
    if language == None: language = flags.arg.language
    if names == None: names = self.item_names(language)

    with self.wf.namespace("name-table"):
      builder = self.wf.task("name-table-builder")
      self.wf.connect(self.wf.read(names, name="name-reader"), builder)
      repo = self.data.name_table(language)
      builder.attach_output("repository", repo)
    return repo

  def build_phrase_table(self, names=None, language=None):
    """Build phrase table for all items."""
    if language == None: language = flags.arg.language
    if names == None: names = self.item_names(language)

    with self.wf.namespace("phrase-table"):
      builder = self.wf.task("phrase-table-builder")
      self.wf.connect(self.wf.read(names, name="name-reader"), builder)
      repo = self.data.phrase_table(language)
      builder.attach_output("repository", repo)
    return repo

def extract_names():
  for language in flags.arg.languages:
    log.info("Extract " + language + " names")
    wf = NamesWorkflow(language + "-name-extraction")
    wf.extract_names(language=language)
    run(wf.wf)

def build_nametab():
  for language in flags.arg.languages:
    log.info("Build " + language + " name table")
    wf = NamesWorkflow(language + "-name-table")
    wf.build_name_table(language=language)
    run(wf.wf)

def build_phrasetab():
  for language in flags.arg.languages:
    log.info("Build " + language + " phrase table")
    wf = NamesWorkflow(language + "-phrase-table")
    wf.build_phrase_table(language=language)
    run(wf.wf)

