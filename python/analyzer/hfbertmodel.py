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

"""Convert HuggingFace BERT PyTorch model to Myelin flow."""

import time
import json
import numpy as np

import sling
import sling.myelin as myelin
import sling.myelin.bert as bert
import sling.myelin.simulator as simulator
import sling.flags as flags

import torch
from transformers import AutoModelForTokenClassification
from transformers import AutoTokenizer
from transformers import AutoConfig

flags.define('--flow')
flags.define('--maxlen', default=None, type=int)
flags.define('--test', default=False, action="store_true")
flags.define('--repeat', default=1, type=int)

flags.parse()

# Load BERT-NER model from HuggingFace.
model_name = "dslim/bert-base-NER"
model = AutoModelForTokenClassification.from_pretrained(model_name)
tokenizer = AutoTokenizer.from_pretrained(model_name)
hfconfig = AutoConfig.from_pretrained(model_name)
uncased = tokenizer.do_lower_case
#print(hfconfig)
#print(model)

length = hfconfig.max_position_embeddings
if flags.arg.maxlen and flags.arg.maxlen < length:
  length = flags.arg.maxlen

# Extract trained parameters.
def extract_embedding(name, module, maxsize=None):
  if maxsize is None:
    bert.params[name] = module.weight.detach().numpy()
  else:
    bert.params[name] = module.weight[:maxsize].detach().numpy()

def extract_linear(name, module):
  bert.params[name + "/w"] = module.weight.detach().numpy()
  bert.params[name + "/b"] = module.bias.detach().numpy()

def extract_layernorm(name, module):
  bert.params[name + "/scale"] = module.weight.detach().numpy()
  bert.params[name + "/bias"] = module.bias.detach().numpy()

extract_embedding("wpe", model.bert.embeddings.word_embeddings)
extract_embedding("segment", model.bert.embeddings.token_type_embeddings)
extract_embedding("position", model.bert.embeddings.position_embeddings, length)
extract_layernorm("embednorm", model.bert.embeddings.LayerNorm)

encoder = model.bert.encoder
for l in range(len(encoder.layer)):
  layer = encoder.layer[l]
  extract_linear("l%d/q" % l, layer.attention.self.query)
  extract_linear("l%d/k" % l, layer.attention.self.key)
  extract_linear("l%d/v" % l, layer.attention.self.value)
  extract_linear("l%d/o" % l, layer.attention.output.dense)
  extract_layernorm("l%d/attnorm" % l, layer.attention.output.LayerNorm)
  extract_linear("l%d/dense1" % l, layer.intermediate.dense)
  extract_linear("l%d/dense2" % l, layer.output.dense)
  extract_layernorm("l%d/ffnorm" % l, layer.output.LayerNorm)


extract_linear("classifier", model.classifier)

# Build flow for transformer model.
flow = myelin.Flow()
f = myelin.Builder(flow, "bert")

# Model configuration.
config = {
  "seq_length": length,
  "hidden_size": hfconfig.hidden_size,
  "num_layers": hfconfig.num_hidden_layers,
  "num_heads": hfconfig.num_attention_heads,
  "filter_size": hfconfig.intermediate_size // hfconfig.hidden_size,
  "vocab_size": hfconfig.vocab_size,
  "num_segment_ids": hfconfig.type_vocab_size,
  "pad_index": hfconfig.pad_token_id,
  "actfn": hfconfig.hidden_act.capitalize(),
  "num_labels": hfconfig._num_labels,
  "uncased": uncased,
}

labels = [None] * hfconfig._num_labels
for label, id in hfconfig.label2id.items():
  labels[id] = label
config["labels"] = labels
flow.blob("config").data = json.dumps(config)

# Vocabulary.
vocabulary = [""] * hfconfig.vocab_size
for subword, index in tokenizer.vocab.items():
    vocabulary[index] = subword
flow.blob("vocabulary").data = "\n".join(vocabulary) + "\n"

inputs = f.var("input", myelin.DT_INT32, [length])
model = bert.BertClassifier(f, config)
output = model(f, inputs)
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

if flags.arg.test or flags.arg.profile:
  # Profile network.
  print("Testing transformer layers:")

  cell = net.cell(f.func.name)
  data = cell.instance()

  if flags.arg.test:
    baseline = simulator.compute(flow, f.func, data)

  start = time.time()
  for n in range(flags.arg.repeat):
    data.clear()
    data.compute()
  end = time.time()
  t = end - start
  tps = (flags.arg.repeat * length) / t

  if flags.arg.profile:
    print(net.profile())

  print("time: %f ms, %f tps" % (t * 1000 / flags.arg.repeat, tps))

  if flags.arg.test:
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

