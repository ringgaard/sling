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

flags.py

import sling.flags
import sling.flags.args as args

flags.arg(...)
flags.init()

flags.args.

import argparse

paser = argparse.ArgumentParser()
corpora.register(flags)
args = flags

corpora.py: definitions for standard corpora and locations
  uses flags for configuring corpora locations

workflow.py: Workflow class subclass of Job with builder methods
  the builder methods can return tasks, channels, resources, or structs
  with information about the component parts
"""

import sling
import sling.pysling as api

class Shard:
  def __init__(self, part, total):
    self.part = part
    self.total = total


class Resource:
  def __init__(self, name, shard, format):
    self.name = name
    self.shard = shard
    self.format = format


class Binding:
  def __init__(self, name, resource):
    self.name = name
    self.resource = resource


class Port:
  def __init__(self, task, name, shard):
    self.task = task
    self.name = name
    self.shard = shard


class Channel:
  def __init__(self, format, producer, consumer):
    self.format = format
    self.producer = producer
    self.consumer = consumer


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


class Job(object):
  def __init__(self):
    self.tasks = []
    self.channels = []
    self.resources = []

  def task(self, type, name=None, shard=None):
    t = Task(type, name, shard)
    self.tasks.append(t)
    return t

  def resource(self, name, shard=None, format="text"):
    r = Resource(name, shard, format)
    self.resources.append(r)
    return r

  def run(self):
    pass

  def wait(self, timeout=None):
    pass

  def done(self):
    return False

  def counters(self):
    return {}

