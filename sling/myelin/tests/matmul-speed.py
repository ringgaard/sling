import sling
import sling.flags as flags
import sling.myelin as myelin
import numpy as np

flags.define("--repeat", default=1, type=int)
flags.define("--m", default=640, type=int)
flags.define("--k", default=480, type=int)
flags.define("--n", default=320, type=int)
flags.define("--size", default=0, type=int)

flags.parse()

compiler = myelin.Compiler()

if flags.arg.size > 0:
  m = flags.arg.size
  k = flags.arg.size
  n = flags.arg.size
else:
  m = flags.arg.m
  k = flags.arg.k
  n = flags.arg.n

flow = myelin.Flow()
f = flow.define("matmul")

A = f.array("A", np.random.rand(m, k).astype(np.float32))
B = f.array("B", np.random.rand(k, n).astype(np.float32))
C = f.array("C", np.zeros(shape=(m, n), dtype=np.float32))

matmul = f.rawop("MatMul")
matmul.add_input(A)
matmul.add_input(B)
matmul.add_output(C)
matmul.add_attr("keep", True)

net = compiler.compile(flow)
cell = net.cell("matmul")

data = cell.instance()

for run in range(flags.arg.repeat):
  data.compute()

print(net.profile())

