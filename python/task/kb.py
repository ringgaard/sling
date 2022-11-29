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

"""Workflow for building knowledge base."""

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task import *

class KnowledgeBaseWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def xref_config(self):
    """Resource for cross-references configuration."""
    return self.wf.resource("xrefs.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def recon_config(self):
    """Resource for reconciler configuration."""
    return self.wf.resource("recon.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def xrefs(self):
    """Resource for store with cross-reference items."""
    return self.wf.resource("xrefs.sling",
                            dir=corpora.kbdir(),
                            format="store/frame")

  def conflicts(self):
    """Resource for store with merge conflicts."""
    return self.wf.resource("conflicts.sling",
                            dir=corpora.kbdir(),
                            format="store/frame")

  def usage(self):
    """Resource for property usage."""
    return self.wf.resource("usage.rec",
                            dir=corpora.kbdir(),
                            format="records/frame",
                            serial=240)

  def topics(self):
    """Resource for public case topics."""
    return self.wf.resource("topics.rec",
                            dir=corpora.workdir("case"),
                            format="records/frame",
                            serial=1000)

  def wikipedia_media(self):
    """Resource for media files from wikipedia."""
    return self.wf.resource("??wiki-media.sling",
                            dir=corpora.workdir("media"),
                            format="text/frame",
                            serial=400)

  def twitter(self):
    """Resource for twitter profiles."""
    return self.wf.resource("twitter-media.sling",
                            dir=corpora.workdir("media"),
                            format="text/frame",
                            serial=410)

  def photos(self):
    """Resource for photo database."""
    return self.wf.resource(corpora.photodb(), format="db/frames", serial=9020)

  def imdb(self):
    """Resource for imdb profile database."""
    return self.wf.resource(corpora.imdb(), format="db/frames", serial=420)

  def celebs(self):
    """Resource for celebrity profiles."""
    return self.wf.resource(corpora.celebdb(), format="db/frames", serial=9010)

  def forum(self):
    """Resource for forum profiles."""
    return self.wf.resource(corpora.forumdb(), format="db/frames", serial=9030)

  def elf(self):
    """Resource for ISO 20275 ELF items."""
    return self.wf.resource("elf.rec",
                            dir=corpora.workdir("org"),
                            format="records/frame",
                            serial=500)

  def nace(self):
    """Resource for NACE items."""
    return self.wf.resource("nace.rec",
                            dir=corpora.workdir("org"),
                            format="records/frame",
                            serial=510)

  def dknace(self):
    """Resource for Danish NACE items."""
    return self.wf.resource("dknace.rec",
                            dir=corpora.workdir("org"),
                            format="records/frame",
                            serial=520)
  def gleif(self):
    """Resource for GLEIF items."""
    return self.wf.resource("gleif.rec",
                            dir=corpora.workdir("org"),
                            format="records/frame",
                            serial=530)

  def cvr(self):
    """Resource for CVR items."""
    return self.wf.resource("cvr.rec",
                            dir=corpora.workdir("org"),
                            format="records/frame",
                            serial=540)

  def factgrid(self):
    """Resources for FactGrid items and properties."""
    return [
      self.wf.resource("factgrid-properties.rec",
                       dir=corpora.workdir("factgrid"),
                       format="records/frame",
                       serial=600),
      self.wf.resource("factgrid-items.rec",
                       dir=corpora.workdir("factgrid"),
                       format="records/frame",
                       serial=610),
    ]

  def kbdb(self):
    """Resource for knowledge base database."""
    return self.wf.resource(corpora.kbdb(), format="db/frames")

  # Item source priority:
  #
  #   10 schemas
  #  100 wikidata properties
  #  110 custom properties
  #  200 wikidata items
  #  210 wikidata redirects
  #  220 fanin
  #  230 popularity
  #  240 usage
  #  300 wikipedia items
  #  310 wikipedia members
  #  320 wikipedia summaries
  #  330 wikilinks
  #  400 wikipedia media
  #  410 twitter
  #  420 imdb
  #  500 elf
  #  510 nace
  #  520 dknace
  #  530 gleif
  #  540 cvr
  #  600 factgrid properties
  #  610 factgrid items
  # 1000 case topics
  # 9010 celebs
  # 9020 photos
  # 9030 forum

  def extended_item_sources(self):
    return self.wf.bundle(
      self.usage(),
      self.topics(),
      self.wikipedia_media(),
      self.twitter(),
      self.imdb(),
      self.celebs(),
      self.forum(),
      self.photos(),
      self.data.wikipedia_summaries(),
      self.elf(),
      self.nace(),
      self.dknace(),
      self.gleif(),
      self.cvr(),
      self.factgrid())

  def collect_xrefs(self):
    """Collect and cluster item identifiers."""
    items = self.wf.bundle(
      self.data.wikidata_properties(),
      self.data.wikidata_redirects(),
      self.data.wikidata_items(),
      self.topics(),
      self.elf(),
      self.nace(),
      self.dknace(),
      self.gleif(),
      self.cvr(),
      self.factgrid(),
      self.celebs())

    with self.wf.namespace("xrefs"):
      builder = self.wf.task("xref-builder", params={"snapshot": True})
      self.wf.connect(self.wf.read(items), builder)
      builder.attach_input("config", self.xref_config())
      xrefs = self.xrefs()
      builder.attach_output("output", xrefs)
      builder.attach_output("conflicts", self.conflicts())
    return xrefs

  def reconcile_items(self, items=None, output=None):
    """Reconcile items."""
    items = self.wf.bundle(
      self.data.schema_defs(),
      self.data.properties(),
      self.data.standard_item_sources(),
      self.extended_item_sources(),
      items)

    if output == None: output = self.data.items()

    with self.wf.namespace("reconciled-items"):
      return self.wf.mapreduce(input=items,
                               output=output,
                               mapper="item-reconciler",
                               reducer="item-merger",
                               format="message/frame",
                               params={
                                 "indexed": True
                               },
                               auxin={
                                 "commons": self.xrefs(),
                                 "config": self.recon_config(),
                               },
                               bufsize=1073741824)

  def fuse_items(self, items=None, extras=None, output=None):
    """Fuse items."""
    items = self.wf.bundle(
      self.data.schema_defs(),
      self.data.standard_item_sources(),
      self.data.wikidata_properties(),
      items,
      extras)
    if output == None: output = self.data.items()

    with self.wf.namespace("fused-items"):
      return self.wf.mapreduce(input=items,
                               output=output,
                               mapper="item-reconciler",
                               reducer="item-merger",
                               format="message/frame",
                               params={"indexed": True},
                               auxin={"config": self.recon_config()})

  def build_knowledge_base(self):
    """Task for building knowledge base store with items, and schemas."""
    items = self.data.items()

    with self.wf.namespace("kb"):
      # Prune information from Wikidata items.
      pruned_items = self.wf.map(items, "wikidata-pruner",
        params={"prune_aliases": True,
                "prune_wiki_links": True,
                "prune_category_members": True})

      # Collect frames into knowledge base store.
      return self.wf.write(pruned_items, self.data.knowledge_base(),
                           params={"string_buckets": 32 * 1024 * 1024})

  def load_items(self):
    """Task for loading items into database."""
    self.wf.write(self.wf.read(self.data.items()), self.kbdb())

  def property_usage(self):
    """Task for computing property usage statistics."""
    usage = self.wf.task("property-usage")
    usage.attach_input("kb", self.data.knowledge_base())
    usage.attach_output("output", self.usage())

def collect_xrefs():
  log.info("Build cross references")
  wf = KnowledgeBaseWorkflow("xref-builder")
  wf.collect_xrefs()
  run(wf.wf)

def reconcile_items():
  log.info("Reconcile items")
  wf = KnowledgeBaseWorkflow("reconcile-items")
  wf.reconcile_items()
  run(wf.wf)

def fuse_items():
  log.info("Fuse items")
  wf = KnowledgeBaseWorkflow("fuse-items")
  wf.fuse_items()
  run(wf.wf)

def build_kb():
  log.info("Build knowledge base repository")
  wf = KnowledgeBaseWorkflow("knowledge-base")
  wf.build_knowledge_base()
  run(wf.wf)

def load_items():
  log.info("Load items into database")
  wf = KnowledgeBaseWorkflow("knowledge-base")
  wf.load_items()
  run(wf.wf)

def property_usage():
  log.info("Property usage")
  wf = KnowledgeBaseWorkflow("property-usage")
  wf.property_usage()
  run(wf.wf)

