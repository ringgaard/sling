# Copyright 2018 Google Inc. All Rights Reserved.
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

"""Numerical gradient checking."""

import sling
import sling.myelin as myelin
import sling.flags as flags
import numpy as np

flags.parse()
compiler = myelin.Compiler()

shape = [16]
dtype = myelin.DT_FLOAT
nptype = np.float32
dtype = "float64"
nptype = np.float64
eps = 1e-3
tolerance = 1e-5

# Compute number of elements in shape.
def elements(shape):
  n = 1
  for d in shape: n *= d
  return n

# Make one-hot tensor with linear indexing.
def onehot(shape, index, value):
  v = np.zeros(elements(shape)).astype(nptype)
  v[index] = value
  return v.reshape(shape)

# Get variable name for adjoint.
def adjoint(v):
  name = v.name
  slash = name.find('/')
  if slash == -1: return "gradients/d_" + name
  return "gradients/" + name[:slash] + "/d_" + name[slash + 1:];

# Check gradient by comparing analytical and numerical derivatives.
def gradcheck(f, inputs, outputs, lo=-10.0, hi=10.0):
  # Get function from flow builder.
  flow = f.flow
  func = f.func

  # Mark inputs and outputs.
  for v in inputs: v.input = True
  for v in outputs: v.output = True

  # Enable backprop for function to compute gradient.
  func.backprop = True

  # Compile flow.
  net = compiler.compile(flow)
  cell = net.cell(func.name)
  gcell = net.cell("gradients/" + func.name)
  primal = gcell.index("gradients/" + func.name + "/primal")

  # Get input and output tensors.
  vin = []
  for v in inputs: vin.append(cell.index(v))
  vout = []
  for v in outputs: vout.append(cell.index(v))

  # Choose random input point for evaluating gradient.
  x = {}
  for v in inputs:
    x[v] = np.random.ranf(v.shape).astype(nptype) * (hi - lo) + lo

  # Compute f(x).
  data = cell.instance()
  for v in inputs: data[v] = x[v]
  data.compute()

  # Check gradient for each output variable element.
  for output in outputs:
    for j in xrange(output.elements()):
      # Compute analytical gradient.
      gdata = gcell.instance()
      gdata[primal] = data
      gdata[adjoint(output)] =  onehot(output.shape, j, 1.0)
      gdata.compute()

      # Check gradient for each input variable element.
      for input in inputs:
        gradient = gdata[adjoint(input)]
        for i in xrange(input.elements()):
          # Construct one-hot tensor with x_i set to epsilon.
          delta = onehot(input.shape, i, eps)

          # Compute f(x-delta) and f(x+delta).
          plus = cell.instance()
          minus = cell.instance()
          for v in inputs:
            if v == input:
              plus[v] = x[v] + delta
              minus[v] = x[v] - delta
            else:
              plus[v] = x[v]
              minus[v] = x[v]
          plus.compute()
          minus.compute()

          # Compute numerical estimate of df_j/d_i as
          # (f_j(x+eps)-f_j(x-eps)) / (2 * eps)
          fplus = plus.tensor(output)[j]
          fminus = minus.tensor(output)[j]
          numerical = (fplus - fminus) / (2 * eps)

          # Compare numerical gradient with analytical gradient.
          analytical = gradient[i]
          error = abs(analytical - numerical)
          if error > tolerance:
            print analytical, "vs", numerical, "(", error, ")"

#flow = myelin.Flow()
#f = flow.define("f")
#x = f.var("x", dtype, shape)
#y = f.square(x, "y")
#gradcheck(f, [x], [y])

flow = myelin.Flow()
f = flow.define("f")
x = f.var("x", dtype, shape)
y = f.exp(x, "y")
gradcheck(f, [x], [y])

#flow = myelin.Flow()
#f = flow.define("f")
#x1 = f.var("x1", dtype, shape)
#x2 = f.var("x2", dtype, shape)
#y = f.div(x1, x2, "y")
#gradcheck(f, [x1, x2], [y])

