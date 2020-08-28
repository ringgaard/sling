package(default_visibility = ["//visibility:public"])

cc_library(
  name = "generic",
  srcs = [
    "argmax.cc",
    "array.cc",
    "concat.cc",
    "gather.cc",
    "generic.cc",
    "reduce.cc",
    "transpose.cc",
  ],
  hdrs = ["generic.h"],
  deps = [
    "//sling/base",
    "//sling/myelin:compute",
    "//sling/myelin:builder",
    "//sling/myelin:simd-assembler",
  ],
)

cc_library(
  name = "arithmetic",
  srcs = [
    "arithmetic.cc",
  ],
  hdrs = ["arithmetic.h"],
  deps = [
    "//sling/base",
    "//sling/myelin:compute",
    "//sling/myelin:express",
    "//sling/myelin/generator:elementwise",
    "//sling/myelin/generator:expression",
  ],
)

cc_library(
  name = "avx",
  srcs = [
    "avx.cc",
    "avx-matmul.cc",
    "simd-matmul.cc",
  ],
  hdrs = [
    "avx.h",
  ],
  deps = [
    "//sling/base",
    "//sling/myelin:compute",
    "//sling/myelin:simd-assembler",
  ],
)

cc_library(
  name = "precompute",
  srcs = ["precompute.cc"],
  hdrs = ["precompute.h"],
  deps = [
    ":arithmetic",
    ":generic",
    "//sling/myelin:compute",
  ],
)

cc_library(
  name = "gradients",
  srcs = ["gradients.cc"],
  hdrs = ["gradients.h"],
  deps = [
    "//sling/myelin:flow",
    "//sling/myelin:gradient",
  ],
)

cc_library(
  name = "library",
  srcs = ["library.cc"],
  hdrs = ["library.h"],
  deps = [
    ":arithmetic",
    ":avx",
    ":generic",
    ":gradients",
    ":precompute",
    "//sling/myelin:compute",
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
    ":arithmetic",
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

