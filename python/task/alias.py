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

class AliasWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def item_aliases(self, language=None):
    """Resource for item aliases in language. This is a set of record files with
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
    return self.wf.resource("aliases@10.rec",
                            dir=corpora.kbdir(language),
                            format="records/alias")

  def alias_corrections(self):
    """Resource for alias corrections."""
    return self.wf.resource("aliases.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def extract_aliases(self, aliases=None, language=None):
    "Task for selecting language-dependent aliases for items."""
    if language == None: language = flags.arg.language

    if aliases == None:
      # Get language-dependent aliases from Wikidata and Wikpedia.
      languages = ",".join(flags.arg.languages)
      wikidata_aliases = self.wf.map(self.data.items(),
                                     "alias-extractor",
                                     params={
                                       "language": language,
                                       "languages": languages,
                                       "skip_aux": True,
                                     },
                                     format="message/alias",
                                     name="wikidata-alias-extractor")

      if flags.arg.wikidata_only:
        wikipedia_aliases = None
      else:
        wikipedia_aliases = []
        for lang in flags.arg.languages:
          wikipedia_aliases.append(
            self.wf.read(self.data.wikipedia_aliases(lang),
                         name=lang + "-wikipedia-alias-reader"))

      aliases = self.wf.collect(wikipedia_aliases, wikidata_aliases)

    with self.wf.namespace("select"):
      # Group aliases on item id.
      output = self.item_aliases(language)
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

  def build_name_table(self, aliases=None, language=None):
    """Build name table for all items."""
    if language == None: language = flags.arg.language
    if aliases == None: aliases = self.item_aliases(language)

    with self.wf.namespace("name-table"):
      builder = self.wf.task("name-table-builder")
      self.wf.connect(self.wf.read(aliases, name="alias-reader"), builder)
      repo = self.data.name_table(language)
      builder.attach_output("repository", repo)
    return repo

  def build_phrase_table(self, aliases=None, language=None):
    """Build phrase table for all items."""
    if language == None: language = flags.arg.language
    if aliases == None: aliases = self.item_aliases(language)

    with self.wf.namespace("phrase-table"):
      builder = self.wf.task("phrase-table-builder")
      self.wf.connect(self.wf.read(aliases, name="alias-reader"), builder)
      repo = self.data.phrase_table(language)
      builder.attach_output("repository", repo)
    return repo

def extract_aliases():
  log.info("Extract " + flags.arg.language + " aliases")
  wf = AliasWorkflow(flags.arg.language + "-alias-extraction")
  wf.extract_aliases(language=flags.arg.language)
  run(wf.wf)

def build_nametab():
  log.info("Build " + flags.arg.language + " name table")
  wf = AliasWorkflow(flags.arg.language + "-name-table")
  wf.build_name_table(language=flags.arg.language)
  run(wf.wf)

def build_phrasetab():
  log.info("Build " + flags.arg.language + " phrase table")
  wf = AliasWorkflow(flags.arg.language + "-phrase-table")
  wf.build_phrase_table(language=flags.arg.language)
  run(wf.wf)

