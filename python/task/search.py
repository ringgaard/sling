# Copyright 2022 Ringgaard Research ApS
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

"""Workflow for building search index."""

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task import *

class SearchWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def search_config(self):
    """Resource for search index configuration."""
    return self.wf.resource("search.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def search_dictionary(self):
    """Resource for search dictionary repository."""
    return self.wf.resource("search-dictionary.repo",
                            dir=corpora.kbdir(),
                            format="repository")

  def search_index(self):
    """Resource for search index repository."""
    return self.wf.resource("search-index.repo",
                            dir=corpora.kbdir(),
                            format="repository")

  def search_vocabulary(self):
    """Resource for search vocabulary."""
    return self.wf.resource("search-vocabulary.map",
                            dir=corpora.kbdir(),
                            format="textmap/term")

  def build_search_dictionary(self, items=None):
    """Task for building search dictionary."""
    if items == None: items = self.data.items()

    with self.wf.namespace("search"):
      builder = self.wf.task("search-dictionary-builder")
      builder.attach_input("config", self.search_config())
      self.wf.connect(self.wf.read(items, name="item-reader"), builder)

      repo = self.search_dictionary()
      builder.attach_output("repository", repo)

    return repo

  def build_search_index(self, items=None):
    """Task for building search dictionary."""
    if items == None: items = self.data.items()

    with self.wf.namespace("search"):
      # Map input items and output entities and terms.
      mapper = self.wf.task("search-index-mapper")
      mapper.attach_input("config", self.search_config())
      mapper.attach_input("dictionary", self.search_dictionary())
      self.wf.connect(self.wf.read(items, name="item-reader"), mapper)
      entities = self.wf.channel(mapper, "entities", format="message/entity")
      terms = self.wf.channel(mapper, "terms", format="message/term")

      # Shuffle terms in bucket order (bucket, termid, entityid).
      postings = self.wf.shuffle(terms, bufsize=256 * 1024 * 1024)

      # Collect entities and terms and build search index.
      builder = self.wf.task("search-index-builder")
      builder.attach_input("config", self.search_config())
      self.wf.connect(entities, builder, name="entities")
      self.wf.connect(postings, builder, name="terms")
      repo = self.search_index()
      builder.attach_output("repository", repo)

    return repo

  def build_search_vocabulary(self, items=None):
    """Task for building search vocabulary."""
    if items == None: items = self.data.items()

    with self.wf.namespace("search"):
      return self.wf.mapreduce(input=items,
                               output=self.search_vocabulary(),
                               mapper="search-vocabulary",
                               reducer="sum-reducer",
                               format="message/term:count",
                               auxin={"config": self.search_config()})

def build_search_dictionary():
  log.info("Build search dictionary")
  wf = SearchWorkflow("search")
  wf.build_search_dictionary()
  run(wf.wf)

def build_search_index():
  log.info("Build search index")
  wf = SearchWorkflow("search")
  wf.build_search_index()
  run(wf.wf)

def build_search_vocabulary():
  log.info("Build search vocabulary")
  wf = SearchWorkflow("search")
  wf.build_search_vocabulary()
  run(wf.wf)

