package(default_visibility = ["//visibility:public"])

cc_library(
  name = "compare-kernels",
  srcs = [
    "compare-kernels.cc",
  ],
  hdrs = [
    "compare-kernels.h",
  ],
  deps = [
    "//base",
    "//file",
    "//myelin:compute",
    "//myelin:flow",
  ],
)

cc_binary(
  name = "kernel-test",
  srcs = ["kernel-test.cc"],
  deps = [
    ":compare-kernels",
    "//base",
    "//file:posix",
    "//myelin/cuda:cuda-runtime",
    "//myelin/kernel:tensorflow",
    "//myelin/kernel:cuda",
  ],
)

cc_binary(
  name = "express-test",
  srcs = ["express-test.cc"],
  deps = [
    "//base",
    "//myelin:express",
  ],
)

cc_binary(
  name = "exprgen-test",
  srcs = ["exprgen-test.cc"],
  deps = [
    "//base",
    "//file",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "wavenet-test",
  srcs = ["wavenet-test.cc"],
  deps = [
    "//base",
    "//base:clock",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin:graph",
    "//myelin:profile",
    "//myelin/kernel:tensorflow",
    "//myelin/kernel:wavenet",
  ],
)

cc_binary(
  name = "tagger-test",
  srcs = ["tagger-test.cc"],
  deps = [
    "//base",
    "//base:clock",
    "//file",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin:graph",
    "//myelin:profile",
    "//myelin/kernel:tensorflow",
    "//myelin/kernel:dragnn",
  ],
)

cc_binary(
  name = "convert-tagger-flow",
  srcs = ["convert-tagger-flow.cc"],
  deps = [
    "//base",
    "//file",
    "//file:posix",
    "//myelin:flow",
  ],
)

cc_binary(
  name = "tdozat-test",
  srcs = ["tdozat-test.cc"],
  deps = [
    "//base",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin:graph",
    "//myelin:multi-process",
    "//myelin:profile",
    "//myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "mnist-test",
  srcs = ["mnist-test.cc"],
  deps = [
    "//base",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin:graph",
    "//myelin:profile",
    "//myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "sempar-test",
  srcs = ["sempar-test.cc"],
  deps = [
    "//base",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin:graph",
    "//myelin:profile",
    "//myelin/kernel:dragnn",
    "//myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "cuda-test",
  srcs = ["cuda-test.cc"],
  deps = [
    "//base",
    "//file:posix",
    "//myelin:compute",
    "//myelin:flow",
    "//myelin:graph",
    "//myelin:profile",
    "//myelin:builder",
    "//myelin/cuda:cuda-runtime",
    "//myelin/kernel:cuda",
    "//myelin/kernel:tensorflow",
  ],
)

