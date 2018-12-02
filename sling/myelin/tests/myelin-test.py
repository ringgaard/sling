import sling
import sling.flags as flags
import sling.myelin as myelin
import numpy as np

flags.define("--dt", default=myelin.DT_FLOAT)

flags.parse()

dt = flags.arg.dt

class Test:
  def __init__(self, f):
    self.name = f.name
    self.runs = 0
    self.errors = 0

tests = {}

compiler = myelin.Compiler()

# Myelin to NumPy type conversion.
nptypes = {
  myelin.DT_FLOAT32: np.float32,
  myelin.DT_FLOAT64: np.float64,
  myelin.DT_INT8: np.int8,
  myelin.DT_INT16: np.int16,
  myelin.DT_INT32: np.int32,
  myelin.DT_INT64: np.int64,
}

# Compute flow function using numpy.
def sigmoid(x):
  return 1 / (1 + np.exp(-x))

def relu(x):
  return np.maximum(x, 0)

def simulate(flow, f, data):
  # Copy input tensors.
  v = {}
  for i in flow.inputs(f):
    if i.data != None:
      v[i] = np.array(i.data, dtype=nptypes[i.type])
    else:
      v[i] = np.asarray(data[i])

  # Get ops in computation order.
  _, ops = flow.order(f)

  # Compute ops using numpy.
  for op in ops:
    i = op.inputs
    o = op.outputs

    if op.type == "MatMul":
      v[o[0]] = np.matmul(v[i[0]], v[i[1]])
    elif op.type == "Exp":
      v[o[0]] = np.exp(v[i[0]])
    elif op.type == "Sigmoid":
      v[o[0]] = sigmoid(v[i[0]])
    elif op.type == "Log":
      v[o[0]] = np.log(v[i[0]])
    elif op.type == "Tanh":
      v[o[0]] = np.tanh(v[i[0]])
    elif op.type == "Relu":
      v[o[0]] = relu(v[i[0]])
    elif op.type == "Sqrt":
      v[o[0]] = np.sqrt(v[i[0]])
    elif op.type == "Square":
      v[o[0]] = np.square(v[i[0]])
    elif op.type == "Neg":
      v[o[0]] = -v[i[0]]
    elif op.type == "Abs":
      v[o[0]] = np.abs(v[i[0]])
    elif op.type == "Add":
      v[o[0]] = v[i[0]] + v[i[1]]
    elif op.type == "Sub":
      v[o[0]] = v[i[0]] - v[i[1]]
    elif op.type == "Mul":
      v[o[0]] = v[i[0]] * v[i[1]]
    elif op.type == "Div":
      v[o[0]] = v[i[0]] / v[i[1]]
    elif op.type == "Minimum":
      v[o[0]] = np.minimum(v[i[0]], v[i[1]])
    elif op.type == "Maximum":
      v[o[0]] = np.maximum(v[i[0]], v[i[1]])
    elif op.type == "Reciprocal":
      v[o[0]] = 1 / v[i[0]]
    elif op.type == "Sum":
      v[o[0]] = np.sum(v[i[0]])
    elif op.type == "Max":
      v[o[0]] = np.max(v[i[0]])
    elif op.type == "Min":
      v[o[0]] = np.min(v[i[0]])
    elif op.type == "Product":
      v[o[0]] = np.prod(v[i[0]])
    elif op.type == "ArgMax":
      v[o[0]] = np.argmax(v[i[0]])
    elif op.type == "ConcatV2":
      n = int(op.attr("N"))
      axis = v[i[n]]
      seq = []
      for k in range(n): seq.append(v[i[k]])
      v[o[0]] = np.concatenate(tuple(seq), axis)
    else:
      raise Exception("No NumPy support for " + op.type)

  # Return results.
  return v

# Compare flow functions against numpy.
def check(flow, lo=-10.0, hi=10.0):
  # Ensure that inputs are not overwritten.
  for i in flow.inputs(): i.output = True

  # Compile flow.
  net = compiler.compile(flow)
  for f in flow.funcs.itervalues():
    # Create data instance for cell.
    cell = net.cell(f.name)
    data = cell.instance()

    # Fill inputs.
    for i in flow.inputs(f):
      if i.data != None: continue
      a = np.asarray(data[i])
      if type(lo) == int and type(hi) == int:
        r = np.random.randint(lo, hi, a.shape)
      else:
        r = np.random.ranf(a.shape) * (hi - lo) + lo
      np.copyto(a, r, casting="unsafe")

    # Compute cell.
    data.compute()

    # Compute function using numpy.
    baseline = simulate(flow, f, data)

    # Check outputs.
    test = tests.get(f.name)
    if test == None:
      test = Test(f)
      tests[f.name] = test
    test.runs += 1
    for o in flow.outputs(f):
      if not np.allclose(data[o], baseline[o]):
        test.errors += 1
        print "mismatch in", f.name, "for", o.name
        print "inputs:"
        for i in flow.inputs(f):
          if i.data == None: print i.name, np.asarray(data[i])
        print "myelin:"
        print np.asarray(data[o])
        print "numpy:"
        print baseline[o]
        print "diff:"
        print baseline[o] - np.asarray(data[o])

def matmul_test(m, k, n):
  flow = myelin.Flow()
  f = flow.define("matmul")
  x = f.var("x", dt, [m, k])
  W = f.var("W", dt, [k, n])
  y = f.matmul(x, W)
  check(flow, -10, 10)

def matmul_add_test(m, k, n):
  flow = myelin.Flow()
  f = flow.define("matmul_add")
  x = f.var("x", dt, [m, k])
  W = f.var("W", dt, [k, n])
  b = f.var("b", dt, [1, n])
  y = f.add(f.matmul(x, W), b)
  check(flow, -10, 10)

def matmul_add_relu_test(m, k, n):
  flow = myelin.Flow()
  f = flow.define("matmul_add_relu")
  x = f.var("x", dt, [m, k])
  W = f.var("W", dt, [k, n])
  b = f.var("b", dt, [1, n])
  y = f.relu(f.add(f.matmul(x, W), b))
  check(flow, -10, 10)

def add_test(n):
  flow = myelin.Flow()
  f = flow.define("add")
  x = f.var("x", dt, [n])
  y = f.var("y", dt, [n])
  x = f.add(x, y)
  check(flow)

def sub_test(n):
  flow = myelin.Flow()
  f = flow.define("sub")
  x = f.var("x", dt, [n])
  y = f.var("y", dt, [n])
  x = f.sub(x, y)
  check(flow)

def mul_test(n):
  flow = myelin.Flow()
  f = flow.define("mul")
  x = f.var("x", dt, [n])
  y = f.var("y", dt, [n])
  x = f.mul(x, y)
  check(flow)

def div_test(n):
  flow = myelin.Flow()
  f = flow.define("div")
  x = f.var("x", dt, [n])
  y = f.var("y", dt, [n])
  x = f.div(x, y)
  check(flow, 1.0, 100.0)
  check(flow, -100.0, -1.0)

def neg_test(n):
  flow = myelin.Flow()
  f = flow.define("neg")
  x = f.var("x", dt, [n])
  y = f.neg(x)
  check(flow)

def abs_test(n):
  flow = myelin.Flow()
  f = flow.define("abs")
  x = f.var("x", dt, [n])
  y = f.abs(x)
  check(flow, -10.0, 10.0)

def exp_test(n):
  flow = myelin.Flow()
  f = flow.define("exp")
  x = f.var("x", dt, [n])
  y = f.exp(x)
  check(flow)

def log_test(n):
  flow = myelin.Flow()
  f = flow.define("log")
  x = f.var("x", dt, [n])
  y = f.log(x)
  check(flow, 0.1, 10.0)

def tanh_test(n):
  flow = myelin.Flow()
  f = flow.define("tanh")
  x = f.var("x", dt, [n])
  y = f.tanh(x)
  check(flow, -1.0, 1.0)

def sqrt_test(n):
  flow = myelin.Flow()
  f = flow.define("sqrt")
  x = f.var("x", dt, [n])
  y = f.sqrt(x)
  check(flow, 0.1, 10.0)

def square_test(n):
  flow = myelin.Flow()
  f = flow.define("square")
  x = f.var("x", dt, [n])
  y = f.square(x)
  check(flow)

def sigmoid_test(n):
  flow = myelin.Flow()
  f = flow.define("sigmoid")
  x = f.var("x", dt, [n])
  y = f.sigmoid(x)
  check(flow)

def softmax_test(n):
  flow = myelin.Flow()
  f = flow.define("softmax")
  x = f.var("x", dt, [n])
  y = f.softmax(x)
  check(flow)

def relu_test(n):
  flow = myelin.Flow()
  f = flow.define("relu")
  x = f.var("x", dt, [n])
  y = f.relu(x)
  check(flow)

def min_test(n):
  flow = myelin.Flow()
  f = flow.define("min")
  x = f.var("x", dt, [n])
  y = f.min(x)
  check(flow)

def max_test(n):
  flow = myelin.Flow()
  f = flow.define("max")
  x = f.var("x", dt, [n])
  y = f.max(x)
  check(flow)

def sum_test(n):
  flow = myelin.Flow()
  f = flow.define("sum")
  x = f.var("x", dt, [n])
  y = f.sum(x)
  check(flow, 0.0, 10.0)

def product_test(n):
  flow = myelin.Flow()
  f = flow.define("product")
  x = f.var("x", dt, [n])
  y = f.product(x)
  check(flow, 0.0, 1.0)

def argmax_test(n):
  flow = myelin.Flow()
  f = flow.define("argmax")
  x = f.var("x", dt, [n])
  y = f.argmax(x)
  check(flow)

def minimum_test(n):
  flow = myelin.Flow()
  f = flow.define("minimum")
  x = f.var("x", dt, [n])
  y = f.var("y", dt, [n])
  z = f.minimum(x, y)
  check(flow)

def maximum_test(n):
  flow = myelin.Flow()
  f = flow.define("maximum")
  x = f.var("x", dt, [n])
  y = f.var("y", dt, [n])
  z = f.maximum(x, y)
  check(flow)

def bcast_test(n):
  flow = myelin.Flow()
  f = flow.define("bcast")
  x = f.var("x", dt, [n])
  y = f.mul(x, f.var("c", dt))
  check(flow)

def concat_test(n, m):
  flow = myelin.Flow()
  f = flow.define("concat")
  a = f.var("a", dt, [1, n])
  b = f.var("b", dt, [1, m])
  c = f.concat([a, b])
  check(flow)

sizes = range(1, 48) + [64, 128, 256]

#for i in sizes:
#  print "test exp", i
# exp_test(i)

#maximum_test(8)
#quit()

print "test concat"
for i in sizes:
  for j in sizes:
    concat_test(i, j)

for i in sizes:
  print "test add", i
  add_test(i)

  print "test sub", i
  sub_test(i)

  print "test mul", i
  mul_test(i)

  print "test div", i
  div_test(i)

  print "test minimum", i
  minimum_test(i)

  print "test maximum", i
  maximum_test(i)

  print "test neg", i
  neg_test(i)

  print "test abs", i
  abs_test(i)

  print "test square", i
  square_test(i)

  print "test relu", i
  relu_test(i)

  print "test bcast", i
  bcast_test(i)

  if dt == myelin.DT_FLOAT or dt == myelin.DT_DOUBLE:
    print "test sqrt", i
    sqrt_test(i)

    print "test exp", i
    exp_test(i)

    print "test tanh", i
    tanh_test(i)

    print "test log", i
    log_test(i)

    print "test sigmoid", i
    sigmoid_test(i)

    print "test softmax", i
    softmax_test(i)

    print "test sum", i
    sum_test(i)

    print "test product", i
    product_test(i)

    print "test min", i
    min_test(i)

    print "test max", i
    max_test(i)

    if dt != myelin.DT_DOUBLE:
      print "test argmax", i
      argmax_test(i)

if dt == myelin.DT_FLOAT:
  for i in sizes:
    print "test matmul", i
    for j in sizes:
      for k in sizes:
        matmul_test(i, j, k)
        matmul_add_test(i, j, k)
        matmul_add_relu_test(i, j, k)

  print "test large matmul"
  matmul_test(2048, 2048, 2048)

print
print "Test results"
print "============"
print

for name in sorted(tests):
  t = tests[name]
  print "%-20s %7d runs %7d errors" % (t.name, t.runs, t.errors)

