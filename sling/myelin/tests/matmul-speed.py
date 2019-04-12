import sling
import sling.flags as flags
import sling.myelin as myelin
import numpy as np

flags.define("--repeat", default=1, type=int)

flags.parse()

compiler = myelin.Compiler()

m = 640
k = 480
n = 320

#m = 1024
#k = 1024
#n = 1024

flow = myelin.Flow()
f = flow.define("matmul")

x = f.array("x", np.random.rand(m, k).astype(np.float32))
W = f.array("W", np.random.rand(k, n).astype(np.float32))
y = f.array("y", np.zeros(shape=(m, n), dtype=np.float32))

matmul = f.rawop("MatMul")
matmul.add_input(x)
matmul.add_input(W)
matmul.add_output(y)
matmul.add_attr("keep", True)

net = compiler.compile(flow)
cell = net.cell("matmul")

data = cell.instance()

for run in xrange(flags.arg.repeat):
  data.compute()

print net.profile()

