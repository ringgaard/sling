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

import os.path

# Utility class for building frames with slots.
class FrameBuilder:
  def __init__(self, store):
    self.store = store
    self.slots = []

  # Add slot.
  def __setitem__(self, name, value):
    self.slots.append((name, value))

  # Add new slot.
  def add(self, name, value):
    self.slots.append((name, value))

  # Add new unique slot.
  def put(self, name, value):
    for slot in self.slots:
      if slot[0] == name and slot[1] == value:
        return False
    self.slots.append((name, value))
    return True

  # Create frame with slots.
  def create(self):
    return self.store.frame(self.slots)

  # Write frame with slots to output.
  def write(self, out):
    frame = self.create()
    out.write(frame.id, frame.data(binary=True))

# Class for keeping track of database checkpoint in file
class Checkpoint:
  def __init__(self, filename):
    self.filename = filename
    self.checkpoint = 0
    if filename is not None and os.path.isfile(filename):
      f = open(filename, 'r')
      self.checkpoint = int(f.read())
      f.close()
      print("read checkpoint", self.checkpoint, "from", filename)

  def commit(self, checkpoint):
    self.checkpoint = checkpoint
    if self.filename is not None:
      f = open(self.filename, 'w')
      f.write(str(checkpoint))
      f.close()
      print("write checkpoint", self.checkpoint, "to", filename)

