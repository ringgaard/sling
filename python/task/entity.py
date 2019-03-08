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

"""Workflow builder for named entity recognition"""

from workflow import *
from wiki import WikiWorkflow
import sling.flags as flags

class EntityWorkflow:
  def __init__(self, name=None, wf=None):
    if wf == None: wf = Workflow(name)
    self.wf = wf
    self.wiki = WikiWorkflow(wf=wf)

  def workdir(self, language=None):
    if language == None:
      return flags.arg.workdir + "/ner"
    else:
      return flags.arg.workdir + "/ner/" + language

  #---------------------------------------------------------------------------
  # Wikipedia link graph
  #---------------------------------------------------------------------------

  def wikilinks(self):
    """Resource for wikipedia link graph."""
    return self.wf.resource("links@10.rec",
                            dir=self.workdir(),
                            format="records/frame")

  def fanin(self):
    """Resource for wikipedia link fan-in."""
    return self.wf.resource("fanin.rec",
                            dir=self.workdir(),
                            format="records/frame")

  def extract_wikilinks(self):
    # Build link graph over all Wikipedias.
    documents = []
    for l in flags.arg.languages:
      documents.extend(self.wiki.wikipedia_documents(l))

    # Extract links from documents.
    mapper = self.wf.task("wikipedia-link-extractor")
    self.wf.connect(self.wf.read(documents), mapper)
    links = self.wf.channel(mapper, format="message/frame", name="output")
    counts = self.wf.channel(mapper, format="message/int", name="fanin")

    # Reduce output links.
    wikilinks = self.wikilinks()
    self.wf.reduce(self.wf.shuffle(links, shards=length_of(wikilinks)),
                   wikilinks, "wikipedia-link-merger")

    # Reduce fan-in.
    fanin = self.fanin()
    self.wf.reduce(self.wf.shuffle(counts, shards=length_of(fanin)),
                   fanin, "item-popularity-reducer")

    return wikilinks, fanin

  #---------------------------------------------------------------------------
  # IDF table
  #---------------------------------------------------------------------------

  def idftable(self, language=None):
    """Resource for IDF table."""
    if language == None: language = flags.arg.language
    return self.wf.resource("idf.repo",
                            dir=self.workdir(language),
                            format="repository")

  def build_idf(self, language=None):
    # Build IDF table from Wikipedia.
    if language == None: language = flags.arg.language
    documents = self.wiki.wikipedia_documents(language)

    with self.wf.namespace(language + "-idf"):
      # Collect words.
      wordcounts = self.wf.shuffle(
        self.wf.map(documents, "vocabulary-mapper", format="message/count",
                    params={
                      "min_document_length": 200,
                      "only_lowercase": True
                    })
      )

      # Build IDF table.
      builder = self.wf.task("idf-table-builder", params={"threshold": 30})
      self.wf.connect(wordcounts, builder)
      builder.attach_output("repository", self.idftable(language))

