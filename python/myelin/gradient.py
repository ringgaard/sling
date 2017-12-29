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

"""Myelin function gradient builder."""

import math
from sling.myelin.builder import Builder

# z = x + y
# dx = dz
# dy = dz
def add_grad(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  g.add(x, g.d(z))
  g.add(y, g.d(z))

# z = x - y
# dx = dz
# dy = -dz
def sub_grad(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  g.add(x, g.d(z))
  g.add(y, g.expr.neg(g.d(z)))

# z = x * y
# dx = dz * y
# dy = x * dz
def mul_grad(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  g.add(x, g.expr.mul(g.d(z), g.v(y)))
  g.add(y, g.expr.mul(g.v(x), g.d(z)))

# z = x * y
# dx = dz * y^T
# dy = x^T * dz
def matmul_grad(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]

  if op.attr("transpose_b"):
    g.add(x, g.expr.matmul(g.d(z), g.v(y)))
  else:
    g.add(x, g.expr.matmul(g.d(z), g.expr.t(g.v(y))))

  if op.attr("transpose_a"):
    g.add(y, g.expr.matmul(g.v(x), g.d(z)))
  else:
    g.add(y, g.expr.matmul(g.expr.t(g.v(x)), g.d(z)))

# z = x / y
# dx = y * dz
# dy = (-x / y^2) * dz
def div_grad(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  dx = g.expr.div(g.d(z), g.v(y))
  g.add(x, dx)
  dy = g.expr.mul(g.d(z), g.expr.div(g.expr.neg(g.v(x)),g.expr.square(g.v(y))))
  g.add(y, )

# y = x * x
# dx = 2 * x * dy
def square_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.expr.mul(2.0, g.v(x))))

# y = 1 / x
# dx = -1 / x^2 * dy
def reciprocal_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.expr.neg(g.expr.rcp(g.expr.square(g.v(x))))))

# y = -x
# dx = -dy
def neg_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.expr.neg(g.v(x))))

# y = sin(x)
# dx = cos(x) * dy
def sin_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.expr.cos(g.v(x))))

# y = cos(x)
# dx = -sin(x) * dy
def cos_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.neg(g.expr.mul(g.d(y), g.expr.sin(g.v(x)))))

# y = exp(x)
# dx = exp(x) * dy = y * dy
def exp_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.v(y)))

# y = log(x)
# dx = dy / x
def log_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.div(g.d(y), g.v(x)))

# y = sigmoid(x)
# dx = sigmoid(x) * (1 - sigmoid(x)) * dy = y * (1 - y) * dy
def sigmoid_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.expr.mul(g.v(y), g.expr.sub(1.0, g.v(y)))))

# y = tanh(x)
# dx = (1 - tanh(x)^2) * dy = (1 - y^2) * dy
def tanh_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g.d(y), g.expr.sub(1.0, g.expr.square(g.v(y)))))

# y = relu(x)
# dx = (x > 0) * dy = relugrad(x, dy)
def relu_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.op("ReluGrad", [g.v(x), g.d(y)]))

# y = x
# dx = dy
def identity_grad(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.d(y))


math_differentiators = {
  'Add': add_grad,
  'Sub': sub_grad,
  'Mul': mul_grad,
  'MatMul': matmul_grad,
  'Div': div_grad,
  'TrueDiv': div_grad,
  'Square': square_grad,
  'Reciprocal': reciprocal_grad,
  'Neg': neg_grad,
  'Sin': sin_grad,
  'Cos': cos_grad,
  'Exp': exp_grad,
  'Log': log_grad,
  'Sigmoid': sigmoid_grad,
  'Tanh': tanh_grad,
  'Relu': relu_grad,
  'Identity': identity_grad,
}

def basename(name):
  slash = name.rfind('/')
  return name[slash + 1:] if slash != -1 else name

class Gradients:
  def __init__(self, flow, primal, vars):
    self.primal = primal
    self.func = flow.func("gradient/" + self.primal.name)
    self.expr = Builder(flow, self.func)
    self.adjoints = {}
    self.terms = {}
    self.refs = {}
    self.instance = self.expr.var("primal", "&resource")
    for v in vars:
      if v.data == None:
        dv = self.expr.var("d_" + basename(v.name), v.type, v.shape)
        dv.ref = v.ref
        self.adjoints[v] = dv
        self.terms[dv] = []
        if v.ref and len(v.consumers) > 0:
          dvacc = self.expr.var("acc_" + basename(v.name), v.type, v.shape)
          dvacc.ref = True
          self.terms[dv].append(dvacc)

  # Get gradient variable for primal variable.
  def d(self, x):
    if x.data != None: return self.expr.const(0.0)
    return self.adjoints[x]

  # Get variable reference.
  def v(self, x):
    if x.data != None: return x
    r = self.refs.get(x)
    if r == None:
      r = self.expr.ref(self.instance, x, name=basename(x.name))
      r.type = x.type
      r.shape = x.shape
      r.ref = True
      self.refs[x] = r
    return r

  # Add term to adjoint for variable.
  def add(self, x, term):
    if x in self.adjoints:
      self.terms[self.adjoints[x]].append(term)


def grad(flow, func, differentiators=math_differentiators):
  # Get variables for gradients.
  func = flow.func(func)
  vars, ops = flow.order(func)

  # Build gradient expressions.
  g = Gradients(flow, func, vars)
  for op in reversed(ops):
    differentiators[op.type](op, g)

  # Sum gradient terms for each variable.
  sum_index = 1
  for v in vars:
    if v not in g.adjoints: continue
    dv = g.adjoints[v]
    terms = g.terms[dv]
    accum = None
    for t in terms:
      if accum == None:
        accum = t
      else:
        accum = g.expr.add(accum, t)
    if accum != None:
      op = flow.op(dv.name)
      op.type = "Identity"
      op.add_input(accum)
      op.add_output(dv)
      g.func.add(op)
      sum_index += 1

    if not v.ref and v.producer != None and dv.producer != None:
      if v.producer.attr("input"): dv.producer.add_attr("input", 1)
      if v.producer.attr("output"): dv.producer.add_attr("output", 1)

  # Return gradient computation function.
  return g.func

