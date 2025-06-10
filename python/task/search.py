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

flags.define("--custom_search_items",
             metavar="RECFILES",
             help="record file with items for custom indexing ")

flags.define("--custom_search_dictionary",
             metavar="REPO",
             help="custom searh dictionary repository")

flags.define("--custom_search_index",
             metavar="REPO",
             help="output repository for custom index")

flags.define("--custom_search_config",
             metavar="CONFIG",
             help="search configuration for custom index")

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
                            dir=corpora.searchdir(),
                            format="repository")

  def search_index(self, aux):
    """Resource for search index repository."""
    fn = "aux-search-index.repo" if aux == 1 else "search-index.repo"
    return self.wf.resource(fn, dir=corpora.searchdir(), format="repository")

  def search_vocabulary(self):
    """Resource for search vocabulary."""
    return self.wf.resource("search-vocabulary.map",
                            dir=corpora.searchdir(),
                            format="textmap/term")

  def custom_search_config(self):
    """Resource for custom search index configuration."""
    return self.wf.resource(flags.arg.custom_search_config,
                            format="store/frame")

  def custom_search_dictionary(self):
    """Resource for custom search dictionary repository."""
    return self.wf.resource(flags.arg.custom_search_dictionary,
                            format="repository")

  def custom_search_items(self):
    """Resource for custom search index repository."""
    return self.wf.resource(flags.arg.custom_search_items,
                            format="records/frame")

  def custom_search_index(self):
    """Resource for custom search index repository."""
    return self.wf.resource(flags.arg.custom_search_index, format="repository")

  def build_search_dictionary(self, items=None, dictionary=None, config=None):
    """Task for building search dictionary."""
    if items is None: items = self.data.items()
    if dictionary is None: dictionary = self.search_dictionary()
    if config is None: config = self.search_config()

    with self.wf.namespace("search"):
      builder = self.wf.task("search-dictionary-builder")
      builder.attach_input("config", config)
      self.wf.connect(self.wf.read(items, name="item-reader"), builder)

      builder.attach_output("repository", dictionary)

    return dictionary

  def build_search_index(self, aux=2, items=None, dictionary=None,
                         repo=None, config=None):
    """Task for building search index."""
    if items is None: items = self.data.items()
    if repo is None: repo = self.search_index(aux)
    if config is None: config = self.search_config()
    if dictionary is None: dictionary = self.search_dictionary()

    with self.wf.namespace("search"):
      # Map input items and output documents and terms.
      mapper = self.wf.task("search-index-mapper", params={"aux": aux})
      mapper.attach_input("config", config)
      mapper.attach_input("dictionary", dictionary)
      self.wf.connect(self.wf.read(items, name="item-reader"), mapper)
      documents = self.wf.channel(mapper, "documents", format="message/sdoc")
      terms = self.wf.channel(mapper, "terms", format="message/term")

      # Shuffle terms in bucket order (bucket, termid, entityid).
      postings = self.wf.shuffle(terms, bufsize=512 * 1024 * 1024)

      # Collect entities and terms and build search index.
      builder = self.wf.task("search-index-builder")
      builder.attach_input("config", config)
      self.wf.connect(documents, builder, name="documents")
      self.wf.connect(postings, builder, name="terms")
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
  wf.build_search_index(0)
  run(wf.wf)

def build_aux_search_index():
  log.info("Build aux search index")
  wf = SearchWorkflow("search")
  wf.build_search_index(1)
  run(wf.wf)

def build_custom_search_dictionary():
  log.info("Build custom search dictionary")
  wf = SearchWorkflow("search")
  wf.build_search_dictionary(
    items=wf.custom_search_items(),
    dictionary=wf.custom_search_dictionary(),
    config=wf.custom_search_config())
  run(wf.wf)

def build_custom_search_index():
  log.info("Build aux search index")
  wf = SearchWorkflow("search")
  wf.build_search_index(2,
                        items=wf.custom_search_items(),
                        dictionary=wf.custom_search_dictionary(),
                        repo=wf.custom_search_index(),
                        config=wf.custom_search_config())
  run(wf.wf)

def build_search_vocabulary():
  log.info("Build search vocabulary")
  wf = SearchWorkflow("search")
  wf.build_search_vocabulary()
  run(wf.wf)
