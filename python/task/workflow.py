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

# Input readers.
readers = {
  "records": "record-file-reader",
  "textmap": "text-map-reader",
  "zip": "zip-file-reader",
  "sling": "frame-store-reader",
  "text": "text-file-reader",
}

def parse_format(format):
  """Parses format specifier into file, key, and value formats."""
  file = "text"
  key = ""
  value = ""
  if format != None:
    slash = format.find('/')
    if slash != -1:
      file = format[:slash]
      colon = format.find(':', slash + 1)
      if colon != -1:
        key = format[slash + 1:colon]
        value = format[colon + 1:]
      else:
        value = format[slash + 1:]
    else:
      file = format
  return file, key, value


def get_reader_task(format):
   f, _, _ = parse_format(format)
   return readers.get(f)


class Reader:
  def __init__(self, inputs, readers):
    self.inputs = inputs
    self.readers = readers

  def __len__(self):
    return len(self.inputs)


class Workflow(Job):
  def __init__(self):
    super(Workflow, self).__init__()

  def reader(self, inputs, name=None):
    if not isinstance(inputs, list):
      type = get_reader_task(inputs.format)
      if type == None: raise Exception("No reader for " + inputs.format)
      reader = self.task(type, name=name)
      reader.attach_input("input", inputs)
      return Reader([inputs], [reader])
    else:
      readers = []
      for part in xrange(len(inputs)):
        shard = Shard(part, len(inputs))
        type = get_reader_task(inputs[part].format)
        reader = self.task(type, name=name, shard=shard)
        reader.attach_input("input", inputs[part])
        readers.append(reader)
      return Reader(inputs, readers)

  def wikidata_dump(self):
    """Resource for wikidata dump"""
    return self.resource(corpora.wikidata_dump(), format="text")

  def wikipedia_dump(self):
    """Resource for wikipedia dump"""
    return self.resource(corpora.wikipedia_dump(), format="xml/wikipage")

