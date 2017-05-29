package(default_visibility = ["//visibility:public"])

cc_library(
  name = "flow",
  srcs = ["flow.cc"],
  hdrs = ["flow.h"],
  deps = [
    "//base",
    "//file",
    "//string:printf",
  ],
)

cc_library(
  name = "compute",
  srcs = [
    "compute.cc",
    "macro-assembler.cc",
  ],
  hdrs = [
    "compute.h",
    "macro-assembler.h",
  ],
  deps = [
    ":flow",
    "//base",
    "//file",
    "//third_party/jit:assembler",
    "//string:printf",
  ],
)

cc_library(
  name = "profile",
  srcs = ["profile.cc"],
  hdrs = ["profile.h"],
  deps = [
    ":compute",
    "//base",
    "//base:clock",
    "//third_party/jit:cpu",
    "//string:printf",
  ],
)

cc_library(
  name = "graph",
  srcs = ["graph.cc"],
  hdrs = ["graph.h"],
  deps = [
    ":flow",
    "//base",
    "//file",
    "//string:printf",
  ],
)

cc_library(
  name = "multi-process",
  srcs = ["multi-process.cc"],
  hdrs = ["multi-process.h"],
  deps = [
    ":compute",
    "//base",
  ],
)

cc_library(
  name = "expression",
  srcs = ["expression.cc"],
  hdrs = ["expression.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "compare-kernels",
  srcs = [
    "compare-kernels.cc",
  ],
  hdrs = [
    "compare-kernels.h",
  ],
  deps = [
    ":compute",
    ":flow",
    "//base",
    "//file",
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

