"""Myelin flow definition for BERT model."""

import time
import numpy as np
import sling.myelin as myelin
import sling.myelin.bert as bert
import sling.myelin.simulator as simulator
import sling.flags as flags

flags.define('--repeat', default=1, type=int)
flags.define('--flow')

flags.parse()

flow = myelin.Flow()
f = myelin.Builder(flow, "f")

config = {
  "seq_length": 128, # 32
  "hidden_size": 768,
  "num_layers": 12, # 12
  "num_heads": 12,
  "filter_size": 4,
  "vocab_size": 28996,
  "num_segment_ids": 2,
  "pad_index": 0,
  "actfn": "Relu",
}

l = config["seq_length"]
inputs = f.var("input", myelin.DT_INT32, [l])
mask = f.var("mask", myelin.DT_FLOAT32, [l])

model = bert.BertModel(f, config)
output = model(f, inputs, mask)
f.rename(output, 'output')

if flags.arg.flow:
  flow.save(flags.arg.flow)

# Compile flow to network.
print("compling...")
start = time.time()
compiler = myelin.Compiler()
net = compiler.compile(flow)
end = time.time()
t = end - start
print("compile time: %f seconds" % t)

# Profile network.
print("Testing transformer layers:")

cell = net.cell(f.func.name)
data = cell.instance()

baseline = simulator.compute(flow, f.func, data)

start = time.time()
for n in range(flags.arg.repeat):
  data.clear()
  data.compute()
end = time.time()
t = end - start
tps = (flags.arg.repeat * l) / t

if flags.arg.profile:
  print(net.profile())

print("time: %f ms, %f tps" % (t * 1000 / flags.arg.repeat, tps))

# Compare output of network to NumPy baseline.
baseline_output = baseline[output]
test_output = np.array(data[output])
if np.allclose(baseline_output, test_output, atol=1):
  print("Baseline comparison: SUCCESS")
else:
  print("Baseline comparison: FAIL")
  print("baseline:");
  print(baseline_output)
  print("test:");
  print(test_output)

