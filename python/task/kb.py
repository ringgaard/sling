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
                            dir=corpora.wikidir(),
                            format="store/frame")

  def collect_xrefs(self):
    """Collect and cluster item identifiers."""
    items = self.wf.bundle(
      self.data.wikidata_redirects(),
      self.data.wikidata_items())

    with self.wf.namespace("xrefs"):
      builder = self.wf.task("xref-builder")
      self.wf.connect(self.wf.read(items), builder)
      builder.attach_input("config", self.xref_config())
      xrefs = self.xrefs()
      builder.attach_output("output", xrefs)
    return xrefs

  def reconcile_items(self, items=None, output=None):
    """Reconcile items."""
    items = self.wf.bundle(self.data.standard_item_sources(), items)
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
                               })

  def fuse_items(self, items=None, extras=None, output=None):
    """Fuse items."""
    items = self.wf.bundle(self.data.standard_item_sources(), items, extras)
    if output == None: output = self.data.items()

    with self.wf.namespace("fused-items"):
      return self.wf.mapreduce(input=items,
                               output=output,
                               mapper="item-reconciler",
                               reducer="item-merger",
                               format="message/frame",
                               params={"indexed": True})

  def build_knowledge_base(self):
    """Task for building knowledge base store with items, properties, and
    schemas."""
    items = self.data.items()
    properties = self.data.wikidata_properties()
    schemas = self.data.schema_defs()

    with self.wf.namespace("kb"):
      # Prune information from Wikidata items.
      pruned_items = self.wf.map(items, "wikidata-pruner",
        params={"prune_aliases": True,
                "prune_wiki_links": True,
                "prune_category_members": True})

      # Collect property catalog.
      property_catalog = self.wf.map(properties, "wikidata-property-collector")

      # Collect frames into knowledge base store.
      parts = self.wf.collect(pruned_items, property_catalog, schemas)
      return self.wf.write(parts, self.data.knowledge_base(),
                           params={"snapshot": True})

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

