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

"""Myelin flow definition for BERT model."""

import numpy as np
import sling.myelin as myelin

def tensor(shape):
  return np.random.randn(*shape).astype(np.float32)

class LayerNorm:
  def __init__(self, f, size, eps=1e-6, name=None):
    self.name = name if name is not None else "LayerNorm"
    self.scale = f.array(self.name + "/scale", tensor([size]))
    self.bias = f.array(self.name + "/bias", tensor([size]))
    self.eps = eps

  def __call__(self, f, x):
    mean = f.mean(x, axis=-1, keepdims=True)
    variance = f.mean(f.square(f.sub(x, mean)), axis=-1, keepdims=True)
    norm = f.mul(f.sub(x, mean),
                 f.div(1.0, f.sqrt(f.add(variance, self.eps))))
    return f.add(f.mul(norm, self.scale), self.bias)

class Linear:
  def __init__(self, f, num_inputs, num_outputs, bias=True, name=None):
    self.name = name if name is not None else "Linear"
    self.weights = f.array(self.name + "/w", tensor([num_outputs, num_inputs]))
    self.bias = f.array(self.name + "/b", tensor([num_outputs])) if bias else None

  def __call__(self, f, x):
    x = f.matmul(x, f.transpose(self.weights))
    if self.bias: x = f.add(x, self.bias)
    return x

class Embedding:
  def __init__(self, f, num_embeddings, embedding_dim, name=None):
    self.name = name if name is not None else "Embedding"
    self.weights = f.array(name, tensor([num_embeddings, embedding_dim]))

  def __call__(self, f, x):
    return f.gather(self.weights, f.reshape(x, x.shape + [1]))

class BertAttention:
  def __init__(self, f, config, layer):
    self.hidden_size = config["hidden_size"]
    self.num_heads = config["num_heads"]
    self.seq_length = config["seq_length"]
    self.depth = self.hidden_size // self.num_heads

    self.query = Linear(f, self.hidden_size, self.hidden_size,
                        name="l%d/k" % layer)
    self.key = Linear(f, self.hidden_size, self.hidden_size,
                      name="l%d/q" % layer)
    self.value = Linear(f, self.hidden_size, self.hidden_size,
                        name="l%d/v" % layer)

    self.output = Linear(f, self.hidden_size, self.hidden_size,
                         name="l%d/o" % layer)
    self.normalize = LayerNorm(f, self.hidden_size, name="l%d/attnorm" % layer)

  def __call__(self, f, x, mask):
    def split_heads(f, x):
      x = f.reshape(x, [self.seq_length, self.num_heads, self.depth])
      return f.transpose(x, [1, 0, 2])

    def combine_heads(f, x):
      x = f.transpose(x, [1, 0, 2])
      return f.reshape(x, [self.seq_length, self.hidden_size])

    # Linearly project the query (q), key (k) and value (v) using different
    # learned projections. This is in preparation of splitting them into
    # multiple heads. Multi-head attention uses multiple queries, keys, and
    # values rather than regular attention (which uses a single q, k, v).
    # Output: [seq_length, hidden_size].
    q = self.query(f, x)
    k = self.key(f, x)
    v = self.value(f, x)

    # Split q, k, v into heads.
    # Output: [num_heads, seq_length, depth].
    q = split_heads(f, q)
    k = split_heads(f, k)
    v = split_heads(f, v)

    # Compute scaled dot-product attention by taking the dot product between
    # "query" and "key" to get the attention scores (energy).
    energy = f.matmul(q, f.transpose(k, [0, 2, 1]))
    energy = f.div(energy, self.depth ** -0.5)

    # Masking.
    if mask is not None:
      energy = f.cond(mask, energy, -1e20)

    # Normalize the attention scores to probabilities.
    attention = f.softmax(energy, axis=-1)

    # Combine attention with values.
    context = f.matmul(attention, v)
    context = combine_heads(f, context)

    y = self.output(f, context)
    y = self.normalize(f, f.add(x, y))
    return y

class BertFeedForward:
  def __init__(self, f, config, layer):
    hidden_size = config["hidden_size"]
    filter_size = config["filter_size"]
    intermediate_size = hidden_size * filter_size
    self.seq_length = config["seq_length"]
    self.actfn = config.get("actfn", "Relu")

    self.dense1 = Linear(f, hidden_size, intermediate_size,
                         name="l%d/dense1" % layer)
    self.dense2 = Linear(f, intermediate_size, hidden_size,
                         name="l%d/dense2" % layer)
    self.normalize = LayerNorm(f, hidden_size, name="l%d/ffnorm" % layer)

  def __call__(self, f, x):
    y = self.dense1(f, x)
    y = f.op(self.actfn, [y])
    y = self.dense2(f, y)
    x = self.normalize(f, f.add(x, y))
    return x

class BertLayer:
  def __init__(self, f, config, layer):
    self.attention = BertAttention(f, config, layer)
    self.ff = BertFeedForward(f, config, layer)

  def __call__(self, f, x, mask):
    x = self.attention(f, x, mask)
    x = self.ff(f, x)
    return x

class BertEncoder:
  def __init__(self, f, config):
    num_layers = config["num_layers"]
    self.layers = []
    for l in range(num_layers):
      layer = BertLayer(f, config, l)
      self.layers.append(layer)

  def __call__(self, f, x, mask=None):
    for layer in self.layers:
      x = layer(f, x, mask)
    return x

class BertEmbeddings:
  def __init__(self, f, config):
    seq_length = config["seq_length"]
    hidden_size = config["hidden_size"]
    vocab_size = config["vocab_size"]
    num_segment_ids = config["num_segment_ids"]
    self.seq_length = seq_length

    # Embeddings.
    self.wpe_embedding = Embedding(f, vocab_size, hidden_size, name="wpe")
    self.segment_embeddings = Embedding(f, num_segment_ids, hidden_size,
                                        name="segment")
    self.positional_embeddings = Embedding(f, seq_length, hidden_size,
                                        name="position")

    # Normalization.
    self.normalize = LayerNorm(f, hidden_size, name="embednorm")

  def __call__(self, f, tokens):
    input_emb = self.wpe_embedding(f, tokens)
    l = self.seq_length
    segments = f.const([0] * l, myelin.DT_INT32, [l], "segment_ids")
    segment_emb = self.segment_embeddings(f, segments)

    # Output: [seq_length, hidden_size]
    embeddings = f.add(input_emb,
                       f.add(segment_emb,
                             self.positional_embeddings.weights))
    embeddings = self.normalize(f, embeddings)
    return embeddings

class BertModel:
  def __init__(self, f, config):
    self.pad_index = config.get("pad_index", -1)
    self.embeddings = BertEmbeddings(f, config)
    self.encoder = BertEncoder(f, config)

  def __call__(self, f, tokens, mask=None):
    if mask is None and self.pad_index != -1:
      mask = f.cond(f.equal(tokens, self.pad_index), 1.0, 0.0)
    x = self.embeddings(f, tokens)
    x = self.encoder(f, x, mask)
    return x

