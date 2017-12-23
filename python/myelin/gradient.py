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

def add_gradient(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  g.add(x, g(z))
  g.add(y, g(z))

def sub_gradient(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  g.add(x, g(z))
  g.add(y, g.expr.negate(g(z)))

def mul_gradient(op, g):
  x = op.inputs[0]
  y = op.inputs[1]
  z = op.outputs[0]
  g.add(x, g.expr.mul(g(z), g.var(y)))
  g.add(y, g.expr.mul(g(z), g.var(x)))

# gx = 2 * x * dy
def square_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g(y), g.expr.mul(2.0, g.var(x))))

# gx = -1 / x^2 * dy
def reciprocal_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  d = g.expr.negate(g.expr.reciprocal(g.expr.square(g.var(x))))
  g.add(x, g.expr.mul(g(y), d))

# gx = cos(x) * dy
def sin_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g(y), g.expr.cos(g.var(x))))

# gx = -sin(x) *dy
def cos_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.negate(g.expr.mul(g(y), g.expr.sin(g.var(x)))))

# gx = exp(x) * dy = y * dy
def exp_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g(y), g.var(y)))

# gx = dy / x
def log_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.div(g(y), g.var(x)))

# gx = sigmoid(x) * (1 - sigmoid(x)) * dy = y * (1 - y) * dy
def sigmoid_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g(y), g.expr.mul(g.var(y), g.expr.sub(1, g.var(y)))))

# gx = (1 - tanh(x)^2) * dy = (1 - y^2) * dy
def tanh_gradient(op, g):
  x = op.inputs[0]
  y = op.outputs[0]
  g.add(x, g.expr.mul(g(y), g.expr.sub(1.0, g.expr.square(g.var(y)))))


math_differentiators = {
  'Add': add_gradient,
  'Sub': sub_gradient,
  'Mul': mul_gradient,
  'Square': square_gradient,
  'Reciprocal': reciprocal_gradient,
  'Sin': sin_gradient,
  'Cos': cos_gradient,
  'Exp': exp_gradient,
  'Log': log_gradient,
  'Sigmoid': sigmoid_gradient,
  'Tanh': tanh_gradient,
}


class Gradients:
  def __init__(self, flow, primal, vars):
    self.primal = primal
    self.func = flow.func("gradient/" + self.primal.name)
    self.expr = Builder(flow, self.func)
    self.adjoints = {}
    self.terms = {}
    self.refs = {}
    self.instance = self.expr.var(self.func.name + "/primal", "&resource")
    for v in vars:
      dv = self.expr.var("g/" + v.name)
      self.adjoints[v] = dv
      self.terms[dv] = []

  # Get gradient variable for primal variable.
  def __call__(self, x):
    if x.data != None: return self.expr.const(0.0)
    return self.adjoints[x]

  # Get variable reference.
  def var(self, x):
    if x.data != None: return x
    r = self.refs.get(x)
    if r == None:
      r = self.expr.ref(self.instance, x, name="ref/" + x.name)
      self.refs[x] = r
    return r

  # Add term to adjoint for variable.
  def add(self, x, term):
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
    dv = g.adjoints[v]
    terms = g.terms[dv]
    accum = None
    for t in terms:
      if accum == None:
        accum = t
      else:
        accum = g.expr.add(accum, t)
    if accum != None:
      op = flow.op(dv.name + "_sum")
      op.type = "Identity"
      op.add_input(accum)
      op.add_output(dv)
      g.func.add(op)
      sum_index += 1

  # Return gradient computation function.
  return g.func

