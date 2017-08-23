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

  def path(self):
    return "annotation/inference_" + self.name + "/" + self.name

  def tfvar(self, name):
    return self.sess.graph.get_tensor_by_name(self.path() + "/" + name + ":0")

  def flowop(self, name):
    return self.flow.op(self.path() + "/" + name)

  def extract(self):
    print "Component", self.name
    component_type = self.spec.network_unit.registered_name
    print "Type:", component_type
    if component_type == 'LSTMNetwork':
      self.extract_lstm()
    elif component_type == 'FeedForwardNetwork':
      self.extract_ff()
    else:
      print "Warning: Unknown component type:", component_type

    for feature in self.spec.fixed_feature:
      print "Fixed feature:", feature.name, "dim:", feature.embedding_dim, "size:", feature.size, "vocab:", feature.vocabulary_size

    for feature in self.spec.linked_feature:
      print "Linked feature:", feature.name, "dim:", feature.embedding_dim, "size:", feature.size

    #print "Spec:", self.spec
    #hidden = self.flow.cnx(self.name + "_h")
    #control = self.flow.cnx(self.name + "_c")

  def extract_lstm(self):
    # Get input and output tensors for LSTM cell.
    h_in_name = "TensorArrayReadV3"
    h_out_name = "lstm_h"
    c_in_name = "TensorArrayReadV3_1"
    c_out_name = "lstm_c"

    tf_h_in = self.tfvar(h_in_name)
    tf_c_in = self.tfvar(c_in_name)
    tf_h_out = self.tfvar(h_out_name)
    tf_c_out = self.tfvar(c_out_name)
    tf_concat = self.tfvar("concat")

    # Extract LSTM cell ops excluding feature inputs.
    self.builder.add(self.func,
                     [tf_h_in, tf_c_in, tf_concat],
                     [tf_h_out, tf_c_out])

    # Set up inputs and outputs.
    for opname in [h_in_name, h_out_name, c_in_name, c_out_name]:
      op = self.flowop(opname)
      op.add_attr("input", "true")
      op.add_attr("output", "true")

  def extract_ff(self):
    pass

  def extract_features(self):
    pass

def main(argv):
  # Load Tensorflow checkpoint for sempar model.
  print "Create session"
  sess = tf.Session()
  saver = tf.train.import_meta_graph(FLAGS.input + "/checkpoints/best.meta")
  saver.restore(sess, FLAGS.input + "/checkpoints/best")

  print "Model loaded"

  # Read master spec.
  print "Get master spec"
  master = sess.graph.get_operation_by_name("annotation/ComputeSession/GetSession")
  master_spec = spec_pb2.MasterSpec()
  master_spec.ParseFromString(master.get_attr("master_spec"))

  # Create flow.
  print "Create flow"
  flow = Flow()
  builder = FlowBuilder(sess, flow)

  # Extract components.
  print "Create components"
  lr = Component(lookup(master_spec.component, "lr_lstm"), builder)
  #rl = Component(lookup(master_spec.component, "rl_lstm"), builder)
  #ff = Component(lookup(master_spec.component, "ff"), builder)

  for component in [lr]:
    print "===================="
    component.extract()

  # Write flow.
  print "Write flow to", FLAGS.output
  flow.save(FLAGS.output)

  print "Done"
if __name__ == '__main__':
  FLAGS.alsologtostderr = True
  tf.app.run()

