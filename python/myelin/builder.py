# Copyright 2017 Google Inc. All Rights Reserved.
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

"""Myelin function builder and expression evaluator."""

import array
from .flow import set_builder_factory, Variable

DT_FLOAT32 = "float32"
DT_FLOAT64 = "float64"

DT_INT8 = "int8"
DT_INT16 = "int16"
DT_INT32 = "int32"
DT_INT64 = "int64"
DT_BOOL = "bool"

DT_INT = DT_INT32
DT_FLOAT = DT_FLOAT32
DT_DOUBLE = DT_FLOAT64

typemap = {
  "f": DT_FLOAT32,
  "d": DT_FLOAT64,
  "i": DT_INT32,
  "l": DT_INT64,
  "B": DT_INT8,
  "h": DT_INT16,
  "b": DT_INT8,
  "q": DT_INT64,
  "?": DT_BOOL,
}

typecodes = {
  DT_FLOAT32: "f",
  DT_FLOAT64: "d",
  DT_INT8: "b",
  DT_INT16: "h",
  DT_INT32: "i",
  DT_INT64: "q",
  DT_BOOL: "?",
}

dtypes = {
  int: DT_INT32,
  float: DT_FLOAT32,
}

# Compute product of elements in list.
def list_product(l):
  n = 1
  for e in l: n *= e
  return n

# Compute the shape of a nested list.
def list_shape(l):
  shape = None
  for e in l:
    if type(e) is list:
      s = list_shape(e)
      if shape is None:
        shape = s
      elif shape != s:
        return None
  if shape is None: return [len(l)]
  return [len(l)] + shape

# Flatten nested list.
def flatten_list(flat, l):
  for e in l:
    if type(e) is list:
      flatten_list(flat, e)
    else:
      flat.append(e)

# Convert nested list to array.
def list_to_array(l, typecode=None):
  # Get list shape.
  shape = list_shape(l)
  if shape is None: raise TypeError("Unsupported list shape")

  # Flatten list.
  f = []
  flatten_list(f, l)

  # Determine list type.
  if typecode is None:
    for e in f:
      et = type(e)
      if et == float or typecode == 'f':
        typecode = 'f'
      elif et == int or typecode == 'i':
        typecode = 'i'
  if typecode is None: typecode = 'f'

  # Convert list to array.
  a = array.array(typecode, f)

  # Return array, type, and shape.
  return a, typecode, shape

class Builder:
  def __init__(self, flow, func=None):
    self.flow = flow
    if func is not None: self.func = flow.func(func)

  def var(self, name, dtype=DT_FLOAT, shape=[]):
    n = self.func.name + "/" + name
    if n in self.flow.vars: raise IndexError("variable already defined: " + n)
    v = self.flow.var(n)
    v.type = dtype
    v.shape = shape
    return v

  def rename(self, var, new_suffix):
    n = self.func.name + "/" + new_suffix
    if n in self.flow.vars: raise IndexError("variable already defined: " + n)
    var.name = n
    return var

  def cnx(self, name, args):
    c = self.flow.cnx(self.func.name + "/" + name)
    for a in args:
      c.add(a)
    return c

  def op(self, optype, args, name=None):
    if name is None:
      name = self.opname(optype)
    else:
      name = self.opname(name)
    op = self.flow.op(name)
    op.type = optype
    self.func.add(op)
    for i in range(len(args)):
      if not isinstance(args[i], Variable): args[i] = self.const(args[i])
      op.add_input(args[i])

    rank = 0
    for a in args:
      if a.rank() > rank: rank = a.rank()
    shape = [1] * rank
    for a in args:
      depth = rank - a.rank()
      for d in range(a.rank()):
        if shape[d + depth] < a.shape[d]:
          shape[d + depth] = a.shape[d]

    dtype = op.inputs[0].type if len(op.inputs) > 0 else DT_FLOAT
    result = self.flow.var(name + ":0", dtype, shape)
    op.add_output(result)
    return result

  def rawop(self, optype, name=None):
    if name is None:
      name = self.opname(optype)
    else:
      name = self.opname(name)
    op = self.flow.op(name)
    op.type = optype
    self.func.add(op)
    return op

  def const(self, value, dtype=None, shape=None, name=None):
    # Scalar type conversion.
    if type(value) is int and dtype == DT_FLOAT: value = float(value)
    if type(value) is float and dtype == DT_INT: value = int(value)

    # Convert scalars and lists.
    if type(value) is float:
      if dtype is None: dtype = DT_FLOAT
      if shape is None: shape = []
    elif type(value) is int:
      if dtype is None: dtype = DT_INT
      if shape is None: shape = []
    elif type(value) is list:
      value, typecode, shape = list_to_array(value, typecodes.get(dtype))
      dtype = typemap[typecode]

    # Convert arrays.
    if type(value) is array.array:
      if dtype is None: dtype = typemap[value.typecode]
      if shape is None: shape = [len(value)]
      value = memoryview(value)

    # Convert other objects supporting the buffer protocol
    try:
      buffer = memoryview(value)
      if dtype is None: dtype = typemap[buffer.format]
      if shape is None: shape = list(buffer.shape)
    except TypeError:
      pass

    # Get type and shape if missing.
    if dtype is None: dtype = str(value.dtype)
    if shape is None: shape = list(value.shape)

    if name is None: name = self.varname("const")
    var = self.flow.var(name, dtype, shape)
    var.data = value
    return var

  def array(self, name, value):
    # Convert lists to arrays that support the buffer protocol.
    if type(value) is list:
      value, _, _ = list_to_array(value)

    # Make constant from object with buffer support.
    view = memoryview(value)
    dtype = typemap[view.format]
    shape = list(view.shape)
    var = self.flow.var(self.varname(name), dtype, shape)
    var.data = value
    return var

  def opname(self, optype):
    name = self.func.name + '/' + optype
    if name not in self.flow.ops: return name
    index = 1
    while True:
      n = name + "_" + str(index)
      if n not in self.flow.ops: return n
      index += 1

  def varname(self, var):
    name = self.func.name + '/' + var
    if name not in self.flow.vars: return name
    index = 1
    while True:
      n = name + "_" + str(index)
      if n not in self.flow.vars: return n
      index += 1

  def concat(self, args, name=None):
    op = self.rawop("Concat", name)
    shape = [args[0].shape[0], 0]
    for arg in args:
      op.add_input(arg)
      shape[1] += arg.shape[1]
    op.add_attr("N", len(args))
    axis = self.const(1, DT_INT)
    op.add_input(axis)
    result = self.var(op.name + ":0", args[0].type, shape)
    op.add_output(result)

    return op.outputs[0]

  def split(self, x, splits, axis=0, name=None):
    op = self.rawop("Split", name)
    op.add_input(x)
    op.add_input(self.const(splits, DT_INT))
    op.add_input(self.const(axis, DT_INT))
    shape = x.shape[:]
    shape[axis] = x.shape[axis] // splits
    results = []
    for n in range(splits):
      o = self.var(op.name + ":" + str(n), x.type, shape)
      op.add_output(o)
      results.append(o)
    return tuple(results)

  def reshape(self, x, shape, name=None):
    result = self.op("Reshape", [x, self.const(shape)], name)
    result.shape = shape
    return result

  def add(self, x, y, name=None):
    return self.op("Add", [x, y], name)

  def sub(self, x, y, name=None):
    return self.op("Sub", [x, y], name)

  def mul(self, x, y, name=None):
    return self.op("Mul", [x, y], name)

  def div(self, x, y, name=None):
    return self.op("Div", [x, y], name)

  def mod(self, x, y, name=None):
    return self.op("Mod", [x, y], name)

  def minimum(self, x, y, name=None):
    return self.op("Minimum", [x, y], name)

  def maximum(self, x, y, name=None):
    return self.op("Maximum", [x, y], name)

  def argm(self, optype, x, axis=None, ouput_value=False, name=None):
    v = self.op(optype, [x], name)
    v.type = DT_INT
    if axis is None:
      v.shape = []
    else:
      if axis < 0: axis = len(x.shape) + axis
      v.shape = x.shape.copy()
      v.producer.add_attr("axis", axis)
      del v.shape[axis]
    if ouput_value:
      m = self.flow.var(v.producer.name + ":1", x.type, v.shape)
      v.producer.add_output(m)
      return v, m
    else:
      return v

  def argmin(self, x, axis=None, ouput_value=False, name=None):
    return self.argm("ArgMin", x, axis, ouput_value, name)

  def argmax(self, x, axis=None, ouput_value=False, name=None):
    return self.argm("ArgMax", x, axis, ouput_value, name)

  def gather(self, params, indices, oov=None, name=None):
    inputs = [params, indices]
    if oov is not None: inputs.append(oov)
    result = self.op("Gather", inputs, name)
    if len(indices.shape) == 0:
      result.shape = params.shape[1:]
    elif len(indices.shape) == 1:
      result.shape = indices.shape + params.shape[1:]
    else:
      result.shape = indices.shape[:-1] + params.shape[indices.shape[-1]:]
    return result

  def pooling_gather(self, optype, params, indices, batch=None, name=None):
    result = self.op(optype, [params, indices], name)
    if len(indices.shape) == 0:
      result.shape = params.shape[1:]
    else:
      result.shape = params.shape[indices.shape[-1]:]
    if batch is not None:
      result.producer.add_attr("batch", batch)
      result.shape = indices.shape[:batch] + result.shape
    return result

  def gather_sum(self, params, indices, batch=None, name=None):
    return self.pooling_gather("GatherSum", params, indices, batch, name)

  def gather_max(self, params, indices, batch=None, name=None):
    return self.pooling_gather("GatherMax", params, indices, batch, name)

  def gather_avg(self, params, indices, batch=None, name=None):
    return self.pooling_gather("GatherAvg", params, indices, batch, name)

  def scatter(self, indices, value, shape, batch=None, pooled=False, name=None):
    result = self.op("Scatter", [indices, value], name)
    if batch is not None: result.producer.add_attr("batch", batch)
    if pooled: result.producer.add_attr("pooled", True)
    result.type = value.type
    result.shape = shape
    return result

  def assign_add_scatter(self, var, indices, value, ref=False, batch=None,
                         pooled=False, name=None):
    op = self.rawop("AssignAddScatter", name)
    if batch is not None: op.add_attr("batch", batch)
    if pooled: result.producer.add_attr("pooled", True)
    op.add_input(var)
    op.add_input(indices)
    op.add_input(value)
    if ref:
      r = self.var(op.name + "/ref", var.type, var.shape)
      r.ref = True
      op.add_output(r)
      return r

  def onehot(self, index, depth, value=None, name=None):
    if value is None:
      result = self.op("OneHot", [index], name)
      result.type = DT_FLOAT
      result.shape = index.shape + [depth]
    else:
      result = self.op("OneHot", [index, value], name)
      result.type = value.type
      result.shape = index.shape + [depth] + value.shape
    result.producer.add_attr("depth", depth)
    return result

  def matmul(self, x, y, name=None):
    result = self.op("MatMul", [x, y], name)
    result.shape = x.shape[:-2] + [x.shape[-2], y.shape[-1]]
    return result

  def transpose(self, x, perm=None, name=None):
    rank = len(x.shape)
    result = self.op("Transpose", [x], name)
    if perm is None and rank == 2:
      # Matrix transpose.
      result.shape = [x.shape[1], x.shape[0]]
    else:
      # Tensor transpose.
      if perm is None: perm = list(reversed(range(rank)))
      if perm == list(range(rank)):
        result.producer.type = "Identity"
        result.shape = x.shape
      else:
        result.producer.add_attr("perm", perm)
        result.shape = [0] * rank
        for d in range(rank): result.shape[d] = x.shape[perm[d]]
    return result

  def t(self, x, perm=None, name=None):
    return self.transpose(x, perm, name)

  def log(self, x, name=None):
    return self.op("Log", [x], name)

  def exp(self, x, name=None):
    return self.op("Exp", [x], name)

  def pow(self, x, y, name=None):
    return self.op("Pow", [x, y], name)

  def erf(self, x, name=None):
    return self.op("Erf", [x], name)

  def sigmoid(self, x, name=None):
    return self.op("Sigmoid", [x], name)

  def relu(self, x, name=None):
    return self.op("Relu", [x], name)

  def gelu(self, x, name=None):
    return self.op("Gelu", [x], name)

  def sin(self, x, name=None):
    return self.op("Sin", [x], name)

  def cos(self, x, name=None):
    return self.op("Cos", [x], name)

  def tan(self, x, name=None):
    return self.op("Tan", [x], name)

  def cot(self, x, name=None):
    return self.op("Cot", [x], name)

  def sec(self, x, name=None):
    return self.op("Sec", [x], name)

  def csc(self, x, name=None):
    return self.op("Csc", [x], name)

  def asin(self, x, name=None):
    return self.op("Asin", [x], name)

  def acos(self, x, name=None):
    return self.op("Acos", [x], name)

  def atan(self, x, name=None):
    return self.op("Atan", [x], name)

  def acot(self, x, name=None):
    return self.op("Acot", [x], name)

  def asec(self, x, name=None):
    return self.op("Asec", [x], name)

  def acsc(self, x, name=None):
    return self.op("Acsc", [x], name)

  def sinh(self, x, name=None):
    return self.op("Sinh", [x], name)

  def cosh(self, x, name=None):
    return self.op("Cosh", [x], name)

  def tanh(self, x, name=None):
    return self.op("Tanh", [x], name)

  def coth(self, x, name=None):
    return self.op("Coth", [x], name)

  def sech(self, x, name=None):
    return self.op("Sech", [x], name)

  def csch(self, x, name=None):
    return self.op("Csch", [x], name)

  def asinh(self, x, name=None):
    return self.op("Asinh", [x], name)

  def acosh(self, x, name=None):
    return self.op("Acosh", [x], name)

  def atanh(self, x, name=None):
    return self.op("Atanh", [x], name)

  def acoth(self, x, name=None):
    return self.op("Acoth", [x], name)

  def asech(self, x, name=None):
    return self.op("Asech", [x], name)

  def acsch(self, x, name=None):
    return self.op("Acsch", [x], name)

  def square(self, x, name=None):
    return self.op("Square", [x], name)

  def sqrt(self, x, name=None):
    return self.op("Sqrt", [x], name)

  def rsqrt(self, x, name=None):
    return self.op("Rsqrt", [x], name)

  def neg(self, x, name=None):
    return self.op("Neg", [x], name)

  def abs(self, x, name=None):
    return self.op("Abs", [x], name)

  def sign(self, x, name=None):
    return self.op("Sign", [x], name)

  def rcp(self, x, name=None):
    return self.op("Reciprocal", [x], name)

  def floor(self, x, name=None):
    return self.op("Floor", [x], name)

  def ceil(self, x, name=None):
    return self.op("Ceil", [x], name)

  def round(self, x, name=None):
    return self.op("Round", [x], name)

  def trunc(self, x, name=None):
    return self.op("Trunc", [x], name)

  def equal(self, x, y, name=None):
    return self.op("Equal", [x, y], name)

  def not_equal(self, x, y, name=None):
    return self.op("NotEqual", [x, y], name)

  def less(self, x, y, name=None):
    return self.op("Less", [x, y], name)

  def less_equal(self, x, y, name=None):
    return self.op("LessEqual", [x, y], name)

  def greater(self, x, y, name=None):
    return self.op("Greater", [x, y], name)

  def greater_equal(self, x, y, name=None):
    return self.op("GreaterEqual", [x, y], name)

  def logical_and(self, x, y, name=None):
    return self.op("And", [x, y], name)

  def logical_or(self, x, y, name=None):
    return self.op("Or", [x, y], name)

  def logical_xor(self, x, y, name=None):
    return self.op("Xor", [x, y], name)

  def logical_not(self, x, name=None):
    return self.op("Not", [x], name)

  def cond(self, c, x, y, name=None):
    return self.op("Cond", [c, x, y], name)

  def select(self, c, x, name=None):
    return self.op("Select", [c, x], name)

  def identity(self, x, name=None):
    return self.op("Identity", [x], name)

  def reduce(self, optype, x, axis=None, keepdims=None, name=None):
    v = self.op(optype, [x], name)
    if axis is None:
      v.shape = []
    else:
      if axis < 0: axis = len(x.shape) + axis
      v.shape = x.shape.copy()
      v.producer.add_attr("axis", axis)
      if keepdims:
        v.shape[axis] = 1
        v.producer.add_attr("keepdims", True)
      else:
        del v.shape[axis]
    return v

  def sum(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("Sum", x, axis, keepdims, name)

  def product(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("Product", x, axis, keepdims, name)

  def min(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("Min", x, axis, keepdims, name)

  def max(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("Max", x, axis, keepdims, name)

  def all(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("All", x, axis, keepdims, name)

  def any(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("Any", x, axis, keepdims, name)

  def count(self, p, axis=None, dtype=DT_FLOAT32, name=None):
    r = self.reduce("Count", p, axis, name)
    r.type = dtype
    return r

  def mean(self, x, axis=None, keepdims=None, name=None):
    sum = self.sum(x, axis, keepdims)
    if axis is None:
      size = list_product(x.shape)
    else:
      if axis < 0: axis = len(x.shape) + axis
      size = x.shape[axis]
    return self.div(sum, self.const(size, x.type), name=name)

  def variance(self, x, axis=None, keepdims=None, name=None):
    average = self.mean(x, axis, keepdims=True)
    return self.mean(self.square(self.sub(x, average)), axis=axis)

  def norm(self, x, name=None):
    return self.sqrt(self.sum(self.square(x)), name)

  def normalize(self, x, name=None):
    return self.mul(x, self.rcp(self.norm(x)), name)

  def softmax(self, x, axis=None, name=None):
    v = self.op("SoftMax", [x], name)
    if axis:
      if axis < 0: axis = len(x.shape) + axis
      v.producer.add_attr("axis", axis)
    return v

  def logsumexp(self, x, axis=None, keepdims=None, name=None):
    return self.reduce("LogSumExp", x, axis, keepdims, name)

  def ref(self, instance, var, name=None):
    r = self.op("Reference", [instance], name)
    r.producer.add_attr("var", var.name)
    r.type = var.type
    r.shape = var.shape
    return r

  def shape(self, x, name=None):
    result = self.op("Shape", [x], name)
    result.shape = [x.rank()]
    result.type = DT_INT
    return result

  def size(self, x, name=None):
    result = self.op("Size", [x], name)
    result.shape = []
    result.type = DT_INT
    return result

  def rank(self, x, name=None):
    result = self.op("Rank", [x], name)
    result.shape = []
    result.type = DT_INT
    return result

  def assign(self, x, y, name=None):
    op = self.rawop("Assign", name)
    op.add_input(x)
    op.add_input(y)


# Set builder factory for flows.
def builder_factory(flow, name):
  return Builder(flow, name)

set_builder_factory(builder_factory)

