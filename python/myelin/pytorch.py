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

# PyTorch to Myelin transpiler (experimental).

import torch
import torch.jit
from .flow import Flow, Variable
from .builder import Builder

dtypes = {
  torch.float32: "float32",
  torch.float64: "float64",
  torch.int32: "int32",
  torch.int64: "int64",
}

optypes = {}

def op(name):
  def inner(func):
    optypes[name] = func
    return func

  return inner

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
      # Get generator method based on node kind.
      kind = node.kind()
      if kind in optypes:
        generator = optypes[kind]
        action = generator(self, node)
      else:
        print("Unknown op", node)
        action = kind

      # Generate standard op if generator returns a string.
      if type(action) is str:
        self.genop(action, node)

    # Mark outputs.
    n = 0
    for output in self.graph.outputs():
      v = self.values[output]
      v.output = True
      if n == 0:
        v.aliases.append("output")
      else:
        v.aliases.append("output:%d" % n)
      n += 1

  def genop(self, opname, node):
    op = self.builder.rawop(opname)
    for input in node.inputs():
      arg = self.var(input)
      op.add_input(arg)

    for output in node.outputs():
      v = self.newvar(output)
      op.add_output(v)

  def var(self, value):
    v = self.values.get(value)
    if type(v) is Variable: return v

    if type(v) is torch.nn.parameter.Parameter: v = v.data
    if type(v) is torch.Tensor: v = v.numpy()
    #print("CONST", value.debugName(), v.type(), type(v.type()))
    var = self.builder.const(v, name=value.debugName())
    self.values[value] = var
    return var

  def newvar(self, value):
    t = value.type()
    name = value.debugName()

    print("new var", name, type(value), type(t), value)
    #print("  value", value)
    #print("  t", t, type(t))
    #print("  dtype", t.dtype())
    #print("  shape",  t.sizes())

    if type(t) is torch.IntType:
      dt = "int64"
      shape = []
    else:
      dt = dtypes[t.dtype()]
      shape = t.sizes()
    v = self.builder.var(name, dt, shape)
    self.values[value] = v
    return v

  @op("prim::GetAttr")
  def op_getattr(self, node):
    # Dereference attribute.
    obj = self.values[node.input()]
    name = node.s("name")
    value = getattr(obj, name)
    self.values[node.output()] = value
    return None

  @op("prim::Constant")
  def op_constant(self, node):
    value = node.output()
    v = value
    t = value.type()
    if type(t) is torch.TensorType:
      v = node.t("value")
    elif type(t) is torch.IntType:
      v = node.i("value")
    elif type(t) is torch.FloatType:
      v = node.f("value")
    elif type(t) is torch.StringType:
      v = node.s("value")
    elif type(t) is torch.NoneType:
      v = None
    elif type(t) is torch.DeviceObjType:
      v = node.s("value")
    elif type(t) is torch.BoolType:
      # TODO: Find a propert way to get bool constant value from node.
      if 'prim::Constant[value=1]' in str(value): v = True
      if 'prim::Constant[value=0]' in str(value): v = False
    else:
      print("Unknown constant type:", value.debugName(), t, type(t))

    print("Constant", t, value.debugName(), v)
    self.values[value] = v

  @op("prim::NumToTensor")
  def op_num_to_tensor(self, node):
    self.values[node.output()] = self.values[node.input()]

  @op("aten::Int")
  def op_num_to_tensor(self, node):
    self.values[node.output()] = int(self.values[node.input()])

  @op("aten::size")
  def op_size(self, node):
    args = list(node.inputs())
    v = self.var(args[0])
    dim = self.values[args[1]]
    size = v.shape[dim]
    self.values[node.output()] = size
    print("const", node.output().debugName(), "=", size)

  @op("prim::ListConstruct")
  def op_list_construct(self, node):
    args = list(node.inputs())
    elems = []
    for arg in args:
      v = self.values[arg]
      if type(v) is Variable: return "Add"
      if type(v) is torch.Tensor and v.size().numel() == 1: v = v.item()
      print("elem", type(v), v)
      elems.append(v)
    self.values[node.output()] = elems
    print("const list", node.output().debugName(), "=", elems)

  @op("aten::add")
  def op_add(self, node):
    args = list(node.inputs())
    sum = 0
    for arg in args:
      if not arg in self.values: return "Add"
      v = self.values[arg]
      if type(v) is Variable: return "Add"
      if type(v) is torch.Tensor and v.size().numel() == 1: v = v.item()
      print("sum arg", type(v), v)
      sum += v
    self.values[node.output()] = sum
    print("sum", node.output().debugName(), "=", sum)

  @op("aten::linear")
  def op_linear(self, node):
    return "Linear"

  @op("aten::relu")
  def op_relu(self, node):
    return "Relu"

