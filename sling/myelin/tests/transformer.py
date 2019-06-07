"""Myelin flow definition for Transformer model."""

import numpy as np
import sling.myelin as myelin
import sling.flags as flags

flags.define("--repeat", default=1, type=int)

class TransformerLayer:
  """Builds a flow graph for single transformer layer."""

  def __init__(self, f, hidden_size, filter_size, seq_length, num_heads):
    self._f = f
    self._hidden_size = hidden_size
    self._filter_size = filter_size
    self._seq_length = seq_length
    self._num_heads = num_heads
    self._depth = hidden_size // num_heads

  def layer_norm(self, x, epsilon=1e-6):
    f = self._f
    scale = f.array('layer_norm_scale',
                    np.random.randn(self._hidden_size).astype(np.float32))
    bias = f.array('layer_norm_bias',
                   np.random.randn(self._hidden_size).astype(np.float32))

    # Computes: tf.reduce_mean(x, axis=[-1], keepdims=True)
    mean = f.mean(x, axis=-1, keepdims=True)

    # Computes: tf.reduce_mean(tf.square(x - mean), axis=[-1], keepdims=True)
    variance = f.mean(f.square(f.sub(x, mean)), axis=-1, keepdims=True)

    # Computes: (x - mean) * tf.rsqrt(variance + epsilon)
    norm_x = f.mul(f.sub(x, mean),
                   f.div(1.0, f.sqrt(f.add(variance, epsilon))))
    return f.add(f.mul(norm_x, scale), bias)

  def self_attention_layer(self, x):
    """Computes self-attention."""

    def _split_heads(x):
      """Split x into different heads, and transpose the resulting value.

      The tensor is transposed to insure the inner dimensions hold the correct
      values during the matrix multiplication.

      Args:
        x: A tensor with shape [seq_length, hidden_size]

      Returns:
        A tensor with shape [num_heads, seq_length, hidden_size/num_heads]
      """
      f = self._f
      x = f.reshape(x, [self._seq_length, self._num_heads, self._depth])
      return f.transpose(x, [1, 0, 2])

    def _combine_heads(x):
      """Combine tensor that has been split.

      Args:
        x: A tensor [num_heads, length, hidden_size/num_heads]

      Returns:
        A tensor with shape [length, hidden_size]
      """

      x = f.transpose(x, [1, 0, 2])  # --> [length, num_heads, depth]
      return f.reshape(x, [self._seq_length, self._hidden_size])

    f = self._f

    q_dense_layer = f.array(
        'q', np.random.randn(
            self._hidden_size, self._hidden_size).astype(np.float32))
    k_dense_layer = f.array(
        'k', np.random.randn(
            self._hidden_size, self._hidden_size).astype(np.float32))
    v_dense_layer = f.array(
        'v', np.random.randn(
            self._hidden_size, self._hidden_size).astype(np.float32))
    output_dense_layer = f.array(
        'o', np.random.randn(
            self._hidden_size, self._hidden_size).astype(np.float32))

    # Linearly project the query (q), key (k) and value (v) using different
    # learned projections. This is in preparation of splitting them into
    # multiple heads. Multi-head attention uses multiple queries, keys, and
    # values rather than regular attention (which uses a single q, k, v).
    # Output: [seq_length, hidden_size].
    q = f.matmul(x, q_dense_layer)
    k = f.matmul(x, k_dense_layer)
    v = f.matmul(x, v_dense_layer)

    # Split q, k, v into heads.
    # Output: [num_heads, seq_length, depth].
    q = _split_heads(q)
    k = _split_heads(k)
    v = _split_heads(v)

    q = f.mul(q, self._depth ** -0.5)

    # Eq: logits = tf.matmul(q, k, transpose_b=True)
    # Logits is supposed to be [num_heads, seq_length, seq_length]
    k = f.transpose(k, [0, 2, 1])
    logits = f.batch_matmul_3d(q, k)

    # We won't need bias if we work with batch_size = 1
    # logits = f.add(logits, bias)

    weights = f.softmax(logits, name='attention_weights')

    # Output: [num_heads, seq_length, depth]
    attention_output = f.batch_matmul_3d(weights, v)

    # Output: [seq_length, hidden_dim]
    attention_output = _combine_heads(attention_output)

    # Do linear projection.
    # Output: [seq_length, hidden_dim]
    attention_output = f.matmul(attention_output, output_dense_layer)
    return attention_output

  def postprocess_wrapper(self, layer_input, layer):
    # LayerNorm.
    layer_input_norm = self.layer_norm(layer_input)

    # Self-attention.
    attention_output = layer(layer_input_norm)

    # Skip-connection.
    output = self._f.add(attention_output, layer_input)
    return output

  def feed_forward_layer(self, x):
    """Computes feed-forward Transformer layer.

    First transforms the input to filter_size and then down-scales to
    hidden_dim.
    """
    f = self._f

    def _ff(x, input_dim, output_dim):
      w = f.array('w',
                  np.random.randn(input_dim, output_dim).astype(np.float32))
      b = f.array('b', np.random.randn(output_dim).astype(np.float32))
      return f.add(f.matmul(x, w), b)

    # Output: [seq_length, filter_size]
    filter_output = f.relu(_ff(x, self._hidden_size, self._filter_size))

    # Output: [seq_length, hidden_size]
    # was: output = _ff(filter_output, self._hidden_size, self._hidden_size)
    output = _ff(filter_output, self._filter_size, self._hidden_size)
    return output

  def build_flow(self, layer_input):
    # Self-attention + LayerNorm+Skip.
    # Output: [seq_length, hidden_dim]
    self_attn_output = self.postprocess_wrapper(layer_input,
                                                self.self_attention_layer)

    # Feed-forward + LayerNorm+Skip.
    # Output: [seq_length, hidden_dim]
    ff_output = self.postprocess_wrapper(self_attn_output,
                                         self.feed_forward_layer)
    # LayerNorm+Skip.
    final_output = self.layer_norm(ff_output)

    return final_output


flags.parse()

flow = myelin.Flow()
f = myelin.Builder(flow, 'f')

hidden_size = 256
filter_size = hidden_size * 4
seq_length = 128
num_heads = 8
num_layers = 1

layer_input = f.var('input', myelin.DT_FLOAT, [seq_length, hidden_size])

for _ in range(num_layers):
  transformer = TransformerLayer(
      f, hidden_size, filter_size, seq_length, num_heads)
  layer_output = transformer.build_flow(layer_input)
  layer_input = layer_output
f.rename(layer_output, 'output')

flow.save('/tmp/transformer.flow')

# Compile flow to network.
compiler = myelin.Compiler()
net = compiler.compile(flow)

# Profile network.
cell = net.cell(f.func.name)
data = cell.instance()

print('Testing tranformer layers:', num_layers, 'hidden:', hidden_size,
      'filter:', filter_size, 'length:', seq_length, 'heads:', num_heads)

for n in range(flags.arg.repeat):
  data.compute()

if flags.arg.profile:
  print(net.profile())

