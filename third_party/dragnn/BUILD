package(default_visibility = ["//visibility:public"])

# You need to install SyntaxNet/DRAGNN to use these targets. First check out
# the Tensorflow models GitHub repository into a directory parallel to the
# SLING directory. Follow the instructions here:
# https://github.com/tensorflow/models/tree/master/syntaxnet

# ln -s ../../../models/syntaxnet third_party/dragnn/syntaxnet

licenses(["notice"])

# IMPORTANT: Ensure that you have followed the instructions in README.first.
cc_library(
  name = "dragnn",
  includes = [
    "syntaxnet",
    "syntaxnet/bazel-genfiles",
  ],
  linkopts = [
    "-lpython2.7",
    "-l:/usr/local/lib/python2.7/dist-packages/dragnn/python/dragnn_cc_impl.so",
  ],
  deps = [
    "//third_party/tensorflow",
  ]
)
