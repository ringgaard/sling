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
    "//myelin/kernel:avx",
    "//myelin/kernel:generic",
    "//myelin/kernel:sse",
  ],
)

