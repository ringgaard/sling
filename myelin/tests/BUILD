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
    "//myelin/kernel:arithmetic",
    "//myelin/kernel:avx",
    "//myelin/kernel:generic",
    "//myelin/kernel:sse",
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
    "//myelin/kernel:avx",
    "//myelin/kernel:generic",
    "//myelin/kernel:sse",
    "//myelin/kernel:dragnn",
    "//myelin/kernel:arithmetic",
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
    "//myelin/kernel:avx",
    "//myelin/kernel:generic",
    "//myelin/kernel:sse",
    "//myelin/kernel:dragnn",
    "//myelin/kernel:arithmetic",
  ],
)

