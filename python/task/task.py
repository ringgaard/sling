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

"""
PyJob in pyapi: interface to task system
task.py: Job, Task, etc classes

corpora.py: definitions for standard corpora and locations
  uses flags for configuring corpora locations

workflow.py: Workflow class subclass of Job with builder methods
  the builder methods can return tasks, channels, resources, or structs
  with information about the component parts
"""

import glob
import os
import re
import sling
import sling.pysling as api

class Shard:
  def __init__(self, part, total):
    self.part = part
    self.total = total

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

  def task(self, type, name=None, shard=None):
    if name == None: name = type  # TODO: generate unique name
    t = Task(type, name, shard)
    self.tasks.append(t)
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
        port = Port(consumer[shard], name, Shard(shard, shards))
        channel[shard].consumer = port
        consumer[shard].connect_source(channel[shard])
    else:
      raise Exception("cannot connect single channel to multiple tasks")

  def run(self):
    pass

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

    return s
