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
fg_properties = res(fgdir + "/factgrid-properties.rec")
items = res(fgdir + "/items.rec")
fanin = res(fgdir + "/fanin.rec")
xrefs = res(fgdir + "/xrefs.sling", "store/frame")
search_dict = res(fgdir + "/search-dictionary.repo", "repository")
search_index = res(fgdir + "/search-index.repo", "repository")
xref_config = res("data/factgrid/xrefs.sling", "store/frame")
recon_config = res("data/factgrid/recon.sling", "store/frame")
search_config = res("data/factgrid/search.sling", "store/frame")
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
  wf.connect(wf.collect(fg_items, fg_properties), xref)
  xref.attach_input("config", xref_config)
  xref.attach_output("output", xrefs)

# Map and reconcile items.
def reconcile_items():
  wf.mapreduce(
    input=wf.bundle(fg_items, fg_properties, datasets.schema_defs(), fanin),
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
  wf.write(wf.read(items), fg_kb, params={"snapshot": True})

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

# Build search index.
def build_search_index():
  # Build search dictionary.
  builder = wf.task("search-dictionary-builder")
  builder.attach_input("config", search_config)
  wf.connect(wf.read(items), builder)
  builder.attach_output("repository", search_dict)

  # Map input items and output entities and terms.
  mapper = wf.task("search-index-mapper")
  mapper.attach_input("config", search_config)
  mapper.attach_input("dictionary", search_dict)
  wf.connect(wf.read(items), mapper)
  entities = wf.channel(mapper, "entities", format="message/entity")
  terms = wf.channel(mapper, "terms", format="message/term")

  # Shuffle terms in bucket order (bucket, termid, entityid).
  postings = wf.shuffle(terms)

  # Collect entities and terms and build search index.
  builder = wf.task("search-index-builder")
  builder.attach_input("config", search_config)
  wf.connect(entities, builder, name="entities")
  wf.connect(postings, builder, name="terms")
  builder.attach_output("repository", search_index)

# Run tasks.
compute_fanin()
collect_xrefs()
reconcile_items()
build_kb()
for language in ["en", "de", "fr"]:
  aliases = extract_aliases(language)
  build_name_table(aliases, language)
build_kb()
build_search_index()
workflow.run(wf)

# Shut down.
workflow.shutdown()

