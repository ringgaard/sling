# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License")

# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Converts sempar model to myelin format
"""

import sys
import numpy as np
import tensorflow as tf

sys.path.insert(0, "third_party/syntaxnet")
sys.path.insert(0, "python")

from flow import Flow
from flow import FlowBuilder
from dragnn.protos import spec_pb2

tf.load_op_library("bazel-bin/nlp/parser/trainer/sempar.so")

flags = tf.app.flags
FLAGS = flags.FLAGS

flags.DEFINE_string("input", "", "Tensorflow input model directory.")
flags.DEFINE_string("output", "", "Myelin output model.")

# DRAGNN op names.
LSTM_H_IN = "TensorArrayReadV3"
LSTM_H_OUT = "lstm_h"
LSTM_C_IN = "TensorArrayReadV3_1"
LSTM_C_OUT = "lstm_c"
LSTM_FV = "concat"

FF_HIDDEN = "Relu"
FF_OUTPUT = "logits"
FF_FV = "concat"

FIXED_EMBEDDING = "/embedding_lookup/Enter"
GET_SESSION = "annotation/ComputeSession/GetSession"

def lookup(elements, name):
  for element in elements:
    if element.name == name: return element
  return None

class Component:
  def __init__(self, spec, builder):
    self.spec = spec
    self.name = spec.name
    self.builder = builder
    self.flow = builder.flow
    self.sess = builder.sess
    self.func = self.flow.func(self.name)
    self.features = None
    self.steps = None

  def path(self):
    return "annotation/inference_" + self.name + "/" + self.name

  def tfvar(self, name):
    return self.sess.graph.get_tensor_by_name(self.path() + "/" + name + ":0")

  def flowop(self, name):
    return self.flow.op(self.path() + "/" + name)

  def flowvar(self, name):
    return self.flow.var(self.path() + "/" + name + ":0")

  def newvar(self, name, type="float", shape=[0], data=None):
    var = self.flowvar(name)
    var.type = type
    var.shape = shape
    var.data = data
    return var

  def newop(self, name, optype, type="float", shape=[0]):
    var = self.newvar(name, type, shape)
    op = self.flowop(name)
    self.func.add(op)
    op.type = optype
    op.add_output(var)
    return op, var

  def extract(self):
    # Extract cell ops for component.
    print "Component", self.name
    component_type = self.spec.network_unit.registered_name
    print "Type:", component_type
    if component_type == 'LSTMNetwork':
      self.extract_lstm()
    elif component_type == 'FeedForwardNetwork':
      self.extract_feed_forward()
    else:
      print "Warning: Unknown component type:", component_type

    # Extract ops for fixed features.
    for feature in self.spec.fixed_feature:
      self.extract_fixed_feature(feature)

    # Extract ops for linked features.
    for feature in self.spec.linked_feature:
      self.extract_linked_feature(feature)

    # Set the number of feature inputs.
    if self.features != None:
      self.features.add_attr("N", len(self.features.inputs))

  def extract_lstm(self):
    # The LSTM cell has inputs and outputs for the hidden and control channels
    # and the input features are collected into a concatenated dense feature
    # vector. First, extract LSTM cell ops excluding feature inputs.
    tf_h_in = self.tfvar(LSTM_H_IN)
    tf_c_in = self.tfvar(LSTM_C_IN)
    tf_h_out = self.tfvar(LSTM_H_OUT)
    tf_c_out = self.tfvar(LSTM_C_OUT)
    tf_fv = self.tfvar(LSTM_FV)
    self.builder.add(self.func,
                     [tf_h_in, tf_c_in, tf_fv],
                     [tf_h_out, tf_c_out])
    self.add_feature_concatenation(self.flowvar(LSTM_FV))

    # The LSTM cells are connected through the hidden and control channels. The
    # hidden output from the previous step is connected to the hidden input
    # for the current step. Likewise, the control output from the previous step
    # is linked to the control input for the current step.
    dims = int(self.spec.network_unit.parameters["hidden_layer_sizes"])

    h_in = self.flowvar(LSTM_H_IN)
    h_out = self.flowvar(LSTM_H_OUT)
    c_in = self.flowvar(LSTM_C_IN)
    c_out = self.flowvar(LSTM_C_OUT)

    h_cnx = self.flow.cnx(self.path() + "/hidden")
    h_cnx.add(h_in)
    h_cnx.add(h_out)

    c_cnx = self.flow.cnx(self.path() + "/control")
    c_cnx.add(c_in)
    c_cnx.add(c_out)

    for v in [h_in, h_out, c_in, c_out]:
      v.type = "&" + v.type
      v.shape = [1, dims]

  def extract_feed_forward(self):
    # The FF cell produce outputs logits as well as step activations from the
    # hidden layer. The input features are collected into a concatenated dense
    # feature vector. First, extract FF cell ops excluding feature inputs.
    tf_hidden = self.tfvar(FF_HIDDEN)
    tf_output = self.tfvar(FF_OUTPUT)
    tf_fv = self.tfvar(FF_FV)
    self.builder.add(self.func, [tf_fv], [tf_hidden, tf_output])
    self.add_feature_concatenation(self.flowvar(FF_FV))

    # The activations from the hidden layer is output to a connector channel in
    # each step and fed back into the cell through the feature functions. A
    # reference variable that points to the channel with all the previous step
    # activations is added to the cell so these can be used by the feature
    # functions to look up activations from previous steps.
    dims = int(self.spec.network_unit.parameters["hidden_layer_sizes"])

    activation = self.flowvar(FF_HIDDEN)
    activation.type = "&" + activation.type
    activation.shape = [1, dims]

    self.steps = self.flow.var(self.path() + "/steps")
    self.steps.type = "&float"
    self.steps.shape = [-1, dims]

    step_cnx = self.flow.cnx(self.path() + "/step")
    step_cnx.add(self.flowvar(FF_HIDDEN))
    step_cnx.add(self.steps)

  def add_feature_concatenation(self, output):
    """Add concat op that the features can be fed into."""
    self.features = self.flowop("concat")
    self.features.type = "ConcatV2"
    self.features.add_output(output)
    self.func.add(self.features)

  def extract_fixed_feature(self, feature):

    print "Fixed feature:", feature.name, "dim:", feature.embedding_dim, "size:", feature.size, "vocab:", feature.vocabulary_size

    # Create feature input variable.
    input = self.flow.var(self.path() + "/" + feature.name)
    input.type = "int32"
    input.shape = [1, feature.size]

    # Extract embedding matrix.
    prefix = "fixed_embedding_" + feature.name
    embedding_name = prefix + FIXED_EMBEDDING
    tf_embedding = self.tfvar(embedding_name)
    self.builder.add(self.func, [], [tf_embedding])
    embedding = self.flowvar(embedding_name)

    # Look up feature(s) in embedding.
    lookup, embedded = self.newop(feature.name + "/Lookup", "Lookup")
    lookup.add_input(input)
    lookup.add_input(embedding)

    # Reshape embedded feature output.
    shape = np.array([1, feature.size * feature.embedding_dim], dtype=np.int32)
    print "shape", shape
    reshape, reshaped = self.newop(feature.name + "/Reshape", "Reshape")
    reshape.add_input(embedded)
    reshape.add_input(self.newvar(feature.name + "/shape", "int32", [2], shape))

    # Add features to feature vector.
    self.features.add_input(reshaped)

  def extract_linked_feature(self, feature):
    print "Linked feature:", feature.name, "dim:", feature.embedding_dim, "size:", feature.size

def main(argv):
  # Load Tensorflow checkpoint for sempar model.
  print "Create session"
  sess = tf.Session()
  saver = tf.train.import_meta_graph(FLAGS.input + "/checkpoints/best.meta")
  saver.restore(sess, FLAGS.input + "/checkpoints/best")

  print "Model loaded"

  # Read master spec.
  print "Get master spec"
  master = sess.graph.get_operation_by_name(GET_SESSION)
  master_spec = spec_pb2.MasterSpec()
  master_spec.ParseFromString(master.get_attr("master_spec"))

  # Create flow.
  print "Create flow"
  flow = Flow()
  builder = FlowBuilder(sess, flow)

  # Extract components.
  print "Create components"
  lr = Component(lookup(master_spec.component, "lr_lstm"), builder)
  rl = Component(lookup(master_spec.component, "rl_lstm"), builder)
  ff = Component(lookup(master_spec.component, "ff"), builder)

  for component in [lr, rl, ff]:
    print "===================="
    component.extract()

  # Write flow.
  print "Write flow to", FLAGS.output
  flow.save(FLAGS.output)

  print "Done"
if __name__ == '__main__':
  FLAGS.alsologtostderr = True
  tf.app.run()

