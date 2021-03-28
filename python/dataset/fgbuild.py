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

"""Build FactGrid knowledge base and alias tables."""

import sling
import sling.flags as flags
import sling.task.workflow as workflow
import sling.task.data as data

flags.parse()

# Start up workflow system.
workflow.startup()

# Create workflow.
wf = workflow.Workflow("factgrid")
datasets = data.Datasets(wf)

# Resources.
def res(files, fmt="records/frame"):
  return wf.resource(files, format=fmt)

fgdir = "data/e/factgrid"
fg_items = res(fgdir + "/factgrid-items.rec")
items = res(fgdir + "/items.rec")
fanin = res(fgdir + "/fanin.rec")
properties = res(fgdir + "/properties.rec")
xrefs = res(fgdir + "/xrefs.sling", "store/frame")
xref_config = res("data/factgrid/xrefs.sling", "store/frame")
recon_config = res("data/factgrid/recon.sling", "store/frame")
fg_kb = res(fgdir + "/factgrid-kb.sling", "store/frame")

# Compute item fanin.
def compute_fanin():
  wf.mapreduce(
    input=fg_items,
    output=fanin,
    mapper="item-fanin-mapper",
    reducer="item-fanin-reducer",
    format="message/int")

# Collect xrefs.
def collect_xrefs():
  xref = wf.task("xref-builder")
  wf.connect(wf.collect(fg_items, fanin), xref)
  xref.attach_input("config", xref_config)
  xref.attach_output("output", xrefs)

# Map and reconcile items.
def reconcile_items():
  wf.mapreduce(
    input=wf.bundle(fg_items, fanin),
    output=items,
    mapper="item-reconciler",
    reducer="item-merger",
    format="message/frame",
    params={
     "indexed": True
    },
    auxin={
     "commons": xrefs,
     "config": recon_config,
    })

# Build knowledge base.
def build_kb():
  property_catalog = wf.map(properties, "wikidata-property-collector")
  parts = wf.collect(items, property_catalog, datasets.schema_defs())
  wf.write(parts, fg_kb, params={"snapshot": True})

# Extract aliases.
def extract_aliases(language):
  # Select aliases
  aliases = wf.map(
    items,
    "alias-extractor",
    params={
     "language": language,
    },
    format="message/alias",
    name="wikidata-alias-extractor")

  # Group aliases on item id.
  aliases_by_item = wf.shuffle(aliases)

  # Filter and select aliases.
  selector = wf.task("alias-selector")
  selector.add_param("language", language)
  wf.connect(aliases_by_item, selector)
  item_aliases = wf.channel(selector, format="message/alias")

  # Group aliases by alias fingerprint.
  aliases_by_fp = wf.shuffle(item_aliases)

  # Merge all aliases for fingerprint.
  output = res(fgdir + "/" + language + "/aliases.rec", "records/alias")
  merger = wf.reduce(aliases_by_fp, output, "alias-merger")
  return output

# Build name table.
def build_name_table(aliases, language):
  builder = wf.task("name-table-builder")
  wf.connect(wf.read(aliases, name="alias-reader"), builder)
  repo = res(fgdir + "/" + language + "/name-table.repo", "repository")
  builder.attach_output("repository", repo)

# Run tasks.
compute_fanin()
collect_xrefs()
reconcile_items()
build_kb()
for language in ["en", "de", "fr"]:
  aliases = extract_aliases(language)
  build_name_table(aliases, language)

workflow.run(wf)

# Shut down.
workflow.shutdown()

