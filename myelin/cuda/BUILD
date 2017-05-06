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

cc_binary(
  name = "cuda-test",
  srcs = ["cuda-test.cc"],
  deps = [
    ":cuda-api",
    "//base",
    "//base:clock",
  ],
)

