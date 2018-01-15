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

"""Interface to SLING task system"""

import glob
import os
import re
import sling
import sling.pysling as api

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
  "store": "frame-store-builder",
  "textmap": "text-map-writer",
  "text": "text-file-writer",
}

class Shard:
  def __init__(self, part, total):
    self.part = part
    self.total = total

  def __hash__(self):
    return hash(self.part)

  def __eq__(self, other):
    return other != None and self.part == other.part

  def __ne__(self, other):
    return not(self == other)

  def __repr__(self):
    return "[%d/%d]" % (self.part, self.total)


class Format:
  def __init__(self, fmt=None, file=None, key=None, value=None):
    if fmt == None:
      self.file = file
      self.key = key
      self.value = value
    else:
      # Parse format specifier into file, key, and value formats.
      # <file>[/[<key>:]<value>].
      self.file = None
      self.key = None
      self.value = None
      slash = fmt.find('/')
      if slash != -1:
        self.file = fmt[:slash]
        colon = fmt.find(':', slash + 1)
        if colon != -1:
          self.key = fmt[slash + 1:colon]
          self.value = fmt[colon + 1:]
        else:
          self.value = fmt[slash + 1:]
      else:
        self.file = fmt

  def as_message(self):
    return Format(file="message", key=self.key, value=self.value)

  def __repr__(self):
    s = self.file if self.file != None else "*"
    if self.key != None or self.value != None:
      s += "/"
      if self.key != None: s += self.key + ":"
      s += self.value if self.value != None else "*"
    return s


class Resource:
  def __init__(self, name, shard, format):
    self.name = name
    self.shard = shard
    self.format = format

  def __repr__(self):
    return "%d/%d" % (self.part, self.total)

  def __repr__(self):
    s = "Resource(" + self.name
    if self.shard != None: s += str(self.shard)
    if self.format != None: s += " as " + str(self.format)
    s += ")"
    return s


class Binding:
  def __init__(self, name, resource):
    self.name = name
    self.resource = resource


class Port:
  def __init__(self, task, name, shard):
    self.task = task
    self.name = name
    self.shard = shard

  def __repr__(self):
    s = str(self.task)
    s += "." + self.name
    if self.shard != None: s += str(self.shard)
    return s


class Channel:
  def __init__(self, format, producer, consumer):
    self.format = format
    self.producer = producer
    self.consumer = consumer

  def __repr__(self):
    s = "Channel("
    if self.producer != None:
      s += str(self.producer)
    else:
      s += "*"
    s += " -> "
    if self.consumer != None:
      s += str(self.consumer)
    else:
      s += "*"
    if self.format != None: s += " as " + str(self.format)
    s += ")"
    return s


class Task:
  def __init__(self, type, name, shard):
    self.type = type
    self.name = name
    self.shard = shard
    self.inputs = []
    self.outputs = []
    self.sources = []
    self.sinks = []
    self.params = {}

  def attach_input(self, name, resource):
    self.inputs.append(Binding(name, resource))

  def attach_output(self, name, resource):
    self.outputs.append(Binding(name, resource))

  def connect_source(self, channel):
    self.sources.append(channel)

  def connect_sink(self, channel):
    self.sinks.append(channel)

  def add_param(self, name, value):
    self.params[name] = str(value)

  def __repr__(self):
    s = self.name
    if self.shard != None: s += str(self.shard)
    return s


class Job(object):
  def __init__(self):
    self.tasks = []
    self.channels = []
    self.resources = []
    self.taskmap = {}
    self.driver = None

  def task(self, type, name=None, shard=None):
    if name == None: name = type
    basename = name
    index = 0
    while (name, shard) in self.taskmap:
      index += 1
      name = basename + "-" + str(index)
    t = Task(type, name, shard)
    self.tasks.append(t)
    self.taskmap[(name, shard)] = t
    return t

  def resource(self, file, dir=None, shards=None, ext=None, format=None):
    # Convert format.
    if type(format) == str: format = Format(format)

    # Combine file name parts.
    filename = file
    if dir != None: filename = os.path.join(dir, filename)
    if shards != None: filename += "@" + str(shards)
    if ext != None: filename += ext

    # Check if filename is a wildcard pattern.
    filenames = []
    if re.search(r"[\*\?\[\]]", filename):
      # Match file name pattern.
      filenames = glob.glob(filename)
    else:
      m = re.match(r"(.*)@(\d+)(.*)", filename)
      if m != None:
        # Expand sharded filename.
        prefix = m.group(1)
        shards = int(m.group(2))
        suffix = m.group(3)
        for shard in xrange(shards):
          fn = "%s-%05d-of-%05d%s" % (prefix, shard, shards, suffix)
          filenames.append(fn)
      else:
        # Simple filename.
        filenames.append(filename)

    # Create resources.
    n = len(filenames)
    if n == 0:
      return None
    elif n == 1:
      r = Resource(filenames[0], None, format)
      self.resources.append(r)
      return r
    else:
      filenames.sort()
      resources = []
      for shard in xrange(n):
        r = Resource(filenames[shard], Shard(shard, n), format)
        self.resources.append(r)
        resources.append(r)
      return resources

  def channel(self, producer, name="output", shards=None, format=None):
    if type(format) == str: format = Format(format)
    if isinstance(producer, list):
      channels = []
      shards = len(producer)
      for shard in xrange(shards):
        sink = Port(producer[i], name, Shard(shard, shards))
        ch = Channel(format, sink, None)
        producer[i].connect_sink(sink)
        channels.append(ch)
        self.channels.append(ch)
      return channels
    elif shards != None:
      channels = []
      for shard in xrange(shards):
        sink = Port(producer, name, Shard(shard, shards))
        ch = Channel(format, sink, None)
        producer.connect_sink(ch)
        channels.append(ch)
        self.channels.append(ch)
      return channels
    else:
      sink = Port(producer, name, None)
      ch = Channel(format, sink, None)
      producer.connect_sink(ch)
      self.channels.append(ch)
      return ch

  def connect(self, channel, consumer, name="input"):
    multi_channel = isinstance(channel, list)
    multi_task = isinstance(consumer, list)
    if not multi_channel and not multi_task:
      # Connect single channel to single task.
      if channel.consumer != None: raise Exception("already connected")
      channel.consumer = Port(consumer, name, None)
      consumer.connect_source(channel)
    elif multi_channel and not multi_task:
      # Connect multiple channels to single task.
      shards = len(channel)
      for shard in xrange(shards):
        if channel[shard].consumer != None: raise Exception("already connected")
        port = Port(consumer, name, Shard(shard, shards))
        channel[shard].consumer = port
        consumer.connect_source(channel)
    elif multi_channel and multi_task:
      # Connect multiple channels to multiple tasks.
      shards = len(channel)
      if len(consumer) != shards: raise Exception("size mismatch")
      for shard in xrange(shards):
        if channel[shard].consumer != None: raise Exception("already connected")
        port = Port(consumer[shard], name, None)
        channel[shard].consumer = port
        consumer[shard].connect_source(channel[shard])
    else:
      raise Exception("cannot connect single channel to multiple tasks")

  def parallel(self, input, threads=5, name=None):
    """Parallelize input messages over thread worker pool."""
    workers = self.task("workers", name=name)
    workers.add_param("worker_threads", threads)
    self.connect(input, workers)
    format = input[0].format if isinstance(input, list) else input.format
    return self.channel(workers, format=format)

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

    # Create writer tasks for writing to output.
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

  def run(self):
    if self.driver != None: raise Exception("job already running")
    self.driver = api.Job(self)

  def wait(self, timeout=None):
    pass

  def done(self):
    return False

  def counters(self):
    return {}

  def dump(self):
    s = ""
    for task in self.tasks:
      s += "task " + task.name
      if task.shard: s += str(task.shard)
      s += " : " + task.type
      s += "\n"
      for param in task.params:
        s += "  param " + param + " = " + task.params[param] + "\n"
      for input in task.inputs:
        s += "  input " + input.name + " = " + str(input.resource) + "\n"
      for source in task.sources:
        s += "  source " +  str(source) + "\n"
      for output in task.outputs:
        s += "  output " + output.name + " = " + str(output.resource) + "\n"
      for sink in task.sinks:
        s += "  sink " +  str(sink) + "\n"
    for channel in self.channels:
      s += "channel " + str(channel) + "\n"
    for resource in self.resources:
      s += "resource " + str(resource) + "\n"
    return s
