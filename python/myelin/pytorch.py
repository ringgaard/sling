# Copyright 2024 Ringgaard Research ApS
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================

# PyTorch to Myelin transpiler.

import torch
import torch.jit
from .flow import Flow, Variable
from .builder import Builder

dtypes = {
  torch.float32: "float32",
}

optypes = {
  "aten::linear": "Linear",
  "aten::relu": "Relu",
}

class Transpiler:
  def __init__(self, flow, name):
    self.builder = flow.define(name)
    self.values = {}

  def trace(self, model, args):
    # Trace model using example inputs.
    trace = torch.jit.trace(model, args)
    self.graph = trace.inlined_graph

    # Set up input arguments.
    for input in self.graph.inputs():
      if input.debugName() == "self.1":
        self.values[input] = model
      else:
        v = self.newvar(input)
        v.input = True

    # Convert graph nodes.
    for node in self.graph.nodes():
      if node.kind() == "prim::GetAttr":
        # Dereference attribute.
        obj = self.values[node.input()]
        name = node.s("name")
        value = getattr(obj, name)
        self.values[node.output()] = value
      else:
        # Generate operation.
        self.generate(node)

    # Mark outputs-
    n = 0
    for output in self.graph.outputs():
      v = self.values[output]
      v.output = True
      if n == 0:
        v.aliases.append("output")
      else:
        v.aliases.append("output:%d" % n)
      n += 1

  def generate(self, node):
    op = self.builder.rawop(optypes.get(node.kind(), node.kind()))
    for input in node.inputs():
      arg = self.var(input)
      op.add_input(arg)

    for output in node.outputs():
      v = self.newvar(output)
      op.add_output(v)

  def var(self, value):
    v = self.values.get(value)
    if type(v) is Variable: return v

    if type(v) is torch.nn.parameter.Parameter: v = v.data.numpy()
    var = self.builder.const(v, name=value.debugName())
    self.values[value] = var
    return var

  def newvar(self, value):
    t = value.type()
    name = value.debugName()
    dt = dtypes[t.dtype()]
    shape = t.sizes()
    v = self.builder.var(name, dt, shape)
    self.values[value] = v
    return v

