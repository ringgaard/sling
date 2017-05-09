package(default_visibility = ["//visibility:public"])

cc_library(
  name = "cuda-api",
  srcs = ["cuda-api.cc"],
  hdrs = ["cuda-api.h"],
  deps = [
    "//base",
  ],
  linkopts = [
    "-ldl",
  ],
)

cc_library(
  name = "cuda",
  srcs = ["cuda.cc"],
  hdrs = ["cuda.h"],
  deps = [
    ":cuda-api",
    "//base",
    "//string:printf",
  ],
)

cc_library(
  name = "cuda-runtime",
  srcs = ["cuda-runtime.cc"],
  hdrs = ["cuda-runtime.h"],
  deps = [
    ":cuda",
    "//base",
    "//myelin:compute",
  ],
)

cc_library(
  name = "cuda-kernel",
  srcs = ["cuda-kernel.cc"],
  hdrs = ["cuda-kernel.h"],
  deps = [
    ":cuda",
    "//base",
    "//myelin:compute",
  ],
)

cc_binary(
  name = "cuda-test",
  srcs = ["cuda-test.cc"],
  deps = [
    ":cuda",
    "//base",
    "//base:clock",
  ],
)

