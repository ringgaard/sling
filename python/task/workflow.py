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

"""Workflow builder"""

from task import *
import corpora
import sling.flags as flags

# Input readers.
readers = {
  "records": "record-file-reader",
  "zip": "zip-file-reader",
  "store": "frame-store-reader",
  "textmap": "text-map-reader",
  "text": "text-file-reader",
}

# Output writers.
writers = {
  "records": "record-file-writer",
  "store": "frame-store-writer",
  "textmap": "text-map-writer",
  "text": "text-file-writer",
}

class WikidataImporter:
  def __init__(self, items, properties):
    self.items = items
    self.properties = properties


class Workflow(Job):
  def __init__(self):
    super(Workflow, self).__init__()

  def read(self, input, name=None):
    if isinstance(input, list):
      outputs = []
      shards = len(input)
      for shard in xrange(shards):
        format = input[shard].format
        if type(format) == str: format = Format(format)
        if format == None: format = Format("text")
        tasktype = readers.get(format.file)
        if tasktype == None: raise Exception("No reader for " + str(format))

        reader = self.task(tasktype, name=name, shard=Shard(shard, shards))
        reader.attach_input("input", input[shard])
        output = self.channel(reader, format=format.as_message())
        output.producer.shard = Shard(shard, shards)
        outputs.append(output)
      return outputs
    else:
      format = input.format
      if type(format) == str: format = Format(format)
      if format == None: format = Format("text")
      tasktype = readers.get(format.file)
      if tasktype == None: raise Exception("No reader for " + str(format))

      reader = self.task(tasktype, name=name)
      reader.attach_input("input", input)
      output = self.channel(reader, format=format.as_message())
      return output

  def write(self, producer, output, sharding=None, name=None):
    # Determine fan-in (channels) and fan-out (files).
    if not isinstance(producer, list): producer = [producer]
    if not isinstance(output, list): output = [output]
    fanin = len(producer)
    fanout = len(output)

    # Use sharding if fan-out is different from fan-in.
    if sharding == None and (fanout != 1 or fanin != fanout):
      sharding = "sharder"
    if sharding == None:
      input = producer
    else:
      sharder = self.task(sharding)
      if fanin == 1:
        self.connect(producer[0], sharder)
      else:
        self.connect(producer, sharder)
      input = self.channel(sharder, shards=fanout, format=producer[0].format)

    # Create writer task for writing to output.
    writer_tasks = []
    for shard in xrange(fanout):
      format = output[shard].format
      if type(format) == str: format = Format(format)
      if format == None: format = Format("text")
      tasktype = writers.get(format.file)
      if tasktype == None: raise Exception("No writer for " + str(format))

      if fanout == 1:
        writer = self.task(tasktype, name=name)
      else:
        writer = self.task(tasktype, name=name, shard=Shard(shard, fanout))
      writer.attach_output("output", output[shard])
      writer_tasks.append(writer)
    self.connect(input, writer_tasks)

  def distribute(self, input, threads=5, name=None):
    workers = self.task("workers", name=name)
    workers.add_param("worker_threads", threads)
    self.connect(input, workers)
    format = input[0].format if isinstance(input, list) else input.format
    return self.channel(workers, format=format)

  def wikidata_dump(self):
    """Resource for wikidata dump"""
    return self.resource(corpora.wikidata_dump(), format="text/json")

  def wikipedia_dump(self):
    """Resource for wikipedia dump"""
    return self.resource(corpora.wikipedia_dump(), format="xml/wikipage")

  def wikidata_import(self, input, name=None):
    """Convert WikiData JSON to SLING items and properties."""
    task = self.task("wikidata-importer", name=name)
    task.add_param("primary_language", flags.arg.language);
    self.connect(input, task)
    items = self.channel(task, name="items", format="message/frame")
    properties = self.channel(task, name="properties", format="message/frame")
    return WikidataImporter(items, properties)

