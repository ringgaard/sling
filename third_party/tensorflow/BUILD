# Tensorflow.

# This uses the Tensorflow C++ API from the Tensorflow Python 2.7 package. You
# need to install this package to use Tensorflow in this way:
#
# sudo apt-get install python-pip python-dev
# pip install tensorflow

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
  name = "tensorflow",
  includes = [
    "include",
  ],
  linkopts = [
    "-lpython2.7",
    "-l:/usr/local/lib/python2.7/dist-packages/tensorflow/python/_pywrap_tensorflow.so",
  ],
)

