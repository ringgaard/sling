package(default_visibility = ["//visibility:public"])

cc_library(
  name = "generic",
  srcs = [
    "array.cc",
    "generic.cc",
    "generic-math.cc",
    "generic-matmul.cc",
    "generic-operators.cc",
  ],
  hdrs = ["generic.h"],
  deps = [
    "//base",
    "//myelin:compute",
  ],
)

cc_library(
  name = "arithmetic",
  srcs = [
    "arithmetic.cc",
  ],
  hdrs = ["arithmetic.h"],
  deps = [
    "//base",
    "//myelin:compute",
    "//myelin:expression",
  ],
)

cc_library(
  name = "sse",
  srcs = [
    "sse.cc",
    "sse-matmul.cc",
  ],
  hdrs = ["sse.h"],
  deps = [
    "//base",
    "//myelin:compute",
  ],
)

cc_library(
  name = "avx",
  srcs = [
    "avx.cc",
    "avx-math.cc",
    "avx-matmul.cc",
    "avx-operators.cc",
  ],
  hdrs = ["avx.h"],
  deps = [
    "//base",
    "//myelin:compute",
  ],
)

cc_library(
  name = "dragnn",
  srcs = ["dragnn.cc"],
  hdrs = ["dragnn.h"],
  deps = [
    "//base",
    "//myelin:compute",
  ],
)

cc_library(
  name = "wavenet",
  srcs = ["wavenet.cc"],
  hdrs = ["wavenet.h"],
  deps = [
    "//base",
    "//myelin:compute",
  ],
)

