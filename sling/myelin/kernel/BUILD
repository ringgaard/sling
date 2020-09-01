package(default_visibility = ["//visibility:public"])

cc_library(
  name = "library",
  srcs = [
    "argmax.cc",
    "arithmetic.cc",
    "array.cc",
    "concat.cc",
    "gather.cc",
    "generic.cc",
    "gradients.cc",
    "library.cc",
    "precompute.cc",
    "reduce.cc",
    "simd-matmul.cc",
    "transpose.cc",
  ],
  hdrs = ["library.h"],
  deps = [
    "//sling/base",
    "//sling/myelin:compute",
    "//sling/myelin:builder",
    "//sling/myelin:flow",
    "//sling/myelin:gradient",
    "//sling/myelin:simd-assembler",
    "//sling/myelin:express",
    "//sling/myelin/generator:elementwise",
    "//sling/myelin/generator:expression",
  ],
)

cc_library(
  name = "cuda",
  srcs = [
    "cublas-matmul.cc",
    "cuda.cc",
    "cuda-array.cc",
    "cuda-matmul.cc",
    "cuda-arithmetic.cc",
  ],
  hdrs = ["cuda.h"],
  deps = [
    ":library",
    "//sling/base",
    "//sling/myelin:compute",
    "//sling/myelin:express",
    "//sling/myelin/cuda:cuda-kernel",
  ],
)

cc_library(
  name = "mkl",
  srcs = ["mkl.cc"],
  hdrs = ["mkl.h"],
  deps = [
    "//sling/base",
    "//sling/myelin:compute",
  ],
  linkopts = [
    "-ldl",
  ],
)

