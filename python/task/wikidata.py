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

"""Workflow for importing and processing Wikidata."""

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task import *

flags.define("--only_primary_language",
             help="only use wikidata labels from primary language",
             default=False,
             action='store_true')

flags.define("--only_known_languages",
             help="only use wikidata labels from known languages",
             default=False,
             action='store_true')

flags.define("--lbzip2",
             help="use lbzip2 for parallel decompression",
             default=False,
             action='store_true')

class WikidataWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def wikidata_dump(self):
    """Resource for Wikidata dump. This can be downloaded from wikimedia.org
    and contains a full dump of Wikidata in JSON format."""
    return self.wf.resource(corpora.wikidata_dump(), format="text/json")

  def wikidatadb(self):
    """Resource for Wikidata database."""
    return self.wf.resource(corpora.wikidatadb(), format="db/frames")

  def wikidata_input(self, dump):
    """Wikidata dump reader with internal or external decompressor."""
    if flags.arg.lbzip2:
      input = self.wf.pipe("lbzip2 -d -c " + dump.name,
                           name="wiki-decompress",
                           format="text/json")
    else:
      input = self.wf.read(dump)
    return self.wf.parallel(input, queue=1024)

  def wikidata_latest(self):
    """Resource for latest Wikidata update. This contains the the QID and
    revision of the latest update."""
    return self.wf.resource("latest", dir=corpora.wikidir(), format="text")

  def wikidata_convert(self, input, name=None):
    """Convert Wikidata JSON to SLING items and properties."""
    task = self.wf.task("wikidata-importer", name=name)
    task.add_param("primary_language", flags.arg.language)
    task.add_param("only_primary_language", flags.arg.only_primary_language)
    task.add_param("only_known_languages", flags.arg.only_known_languages)
    task.attach_output("latest", self.wikidata_latest())

    self.wf.connect(input, task)
    items = self.wf.channel(task, name="items", format="message/frame")
    properties = self.wf.channel(task, name="properties",
                                 format="message/frame")
    return items, properties

  def wikidata_import(self, dump=None):
    """Import Wikidata dump to frame format. It takes a Wikidata dump in JSON
    format as input and converts each item and property to a SLING frame.
    Returns the item and property output files."""
    if dump == None: dump = self.wikidata_dump()
    with self.wf.namespace("wikidata"):
      input = self.wikidata_input(dump)
      items, properties = self.wikidata_convert(input)

      items_output = self.data.wikidata_items()
      self.wf.write(items, items_output, name="item-writer")

      properties_output = self.data.wikidata_properties()
      self.wf.write(properties, properties_output, name="property-writer")

      return items_output, properties_output

  def wikidata_load(self, dump=None):
    """Load Wikidata dump into a database. It takes a Wikidata dump in JSON
    format as input and converts each item and property to a SLING frame."""
    if dump == None: dump = self.wikidata_dump()
    with self.wf.namespace("wikidata"):
      input = self.wikidata_input(dump)
      items, properties = self.wikidata_convert(input)
      db = self.wikidatadb()
      self.wf.write([items, properties], db, name="db-writer",
                    params={"db_write_mode": 3})
      return db

  def wikidata_snapshot(self):
    """Read snapshot from Wikidata database and output items, properties, and
    redirects."""
    with self.wf.namespace("wikisnap"):
      # Read frames from database.
      snapshot = self.wf.read(self.wikidatadb(), name="db-reader",
                              params={"stream": True})
      input = self.wf.parallel(snapshot, queue=16384)

      # Split snapshot into items, properties, and redirects.
      task = self.wf.task("wikidata-splitter")
      self.wf.connect(input, task)
      items = self.wf.channel(task, name="items",
                              format="message/frame")
      properties = self.wf.channel(task, name="properties",
                                   format="message/frame")
      redirects = self.wf.channel(task, name="redirects",
                                  format="message/frame")

      # Write items, properties, and redirects.
      self.wf.write(items, self.data.wikidata_items(),
                    name="item-writer")
      self.wf.write(properties, self.data.wikidata_properties(),
                    name="property-writer")
      self.wf.write(redirects, self.data.wikidata_redirects(),
                    name="redirect-writer")

  def compute_fanin(self):
    """Compute item fan-in, i.e. the number of times an item is the target in
    a relation."""
    return self.wf.mapreduce(input=self.data.wikidata_items(),
                             output=self.data.fanin(),
                             mapper="item-fanin-mapper",
                             reducer="item-fanin-reducer",
                             params={"buckets": 16 * 1024 * 1024},
                             format="message/int")

def import_wikidata():
  log.info("Import wikidata")
  wf = WikidataWorkflow("wikidata-import")
  wf.wikidata_import()
  run(wf.wf)

def load_wikidata():
  log.info("Load wikidata")
  wf = WikidataWorkflow("wikidata-load")
  wf.wikidata_load()
  run(wf.wf)

def snapshot_wikidata():
  log.info("Snapshot wikidata")
  wf = WikidataWorkflow("wikidata-snapshot")
  wf.wikidata_snapshot()
  run(wf.wf)

def compute_fanin():
  log.info("Compute item fan-in")
  wf = WikidataWorkflow("fanin")
  wf.compute_fanin()
  run(wf.wf)

