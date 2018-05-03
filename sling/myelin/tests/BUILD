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
    "//sling/base",
    "//sling/file",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
  ],
)

cc_binary(
  name = "kernel-test",
  srcs = ["kernel-test.cc"],
  deps = [
    ":compare-kernels",
    "//sling/base",
    "//sling/file:posix",
    "//sling/myelin/cuda:cuda-runtime",
    "//sling/myelin/kernel:tensorflow",
    "//sling/myelin/kernel:cuda",
  ],
)

cc_binary(
  name = "express-test",
  srcs = ["express-test.cc"],
  deps = [
    "//sling/base",
    "//sling/myelin:express",
  ],
)

cc_binary(
  name = "exprgen-test",
  srcs = ["exprgen-test.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "wavenet-test",
  srcs = ["wavenet-test.cc"],
  deps = [
    "//sling/base",
    "//sling/base:clock",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:profile",
    "//sling/myelin/kernel:tensorflow",
    "//sling/myelin/kernel:wavenet",
  ],
)

cc_binary(
  name = "tagger-test",
  srcs = ["tagger-test.cc"],
  deps = [
    "//sling/base",
    "//sling/base:clock",
    "//sling/file",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:profile",
    "//sling/myelin/cuda:cuda-runtime",
    "//sling/myelin/kernel:cuda",
    "//sling/myelin/kernel:dragnn",
    "//sling/myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "postagger",
  srcs = ["postagger.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/myelin:builder",
    "//sling/myelin:compute",
    "//sling/myelin:elf-linker",
    "//sling/myelin:gradient",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:learning",
    "//sling/myelin:profile",
    "//sling/myelin/kernel:tensorflow",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)

cc_binary(
  name = "lextagger",
  srcs = ["lextagger.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/file:recordio",
    "//sling/frame:serialization",
    "//sling/frame:store",
    "//sling/myelin:builder",
    "//sling/myelin:compute",
    "//sling/myelin:elf-linker",
    "//sling/myelin:gradient",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:learning",
    "//sling/myelin:profile",
    "//sling/myelin/kernel:tensorflow",
    "//sling/nlp/document",
    "//sling/nlp/document:lexical-encoder",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)

cc_binary(
  name = "convert-tagger-flow",
  srcs = ["convert-tagger-flow.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:posix",
    "//sling/myelin:flow",
  ],
)

cc_binary(
  name = "tdozat-test",
  srcs = ["tdozat-test.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:multi-process",
    "//sling/myelin:profile",
    "//sling/myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "mnist-test",
  srcs = ["mnist-test.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:profile",
    "//sling/myelin/cuda:cuda-runtime",
    "//sling/myelin/kernel:cuda",
    "//sling/myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "sempar-test",
  srcs = ["sempar-test.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:profile",
    "//sling/myelin/kernel:dragnn",
    "//sling/myelin/kernel:tensorflow",
  ],
)

cc_binary(
  name = "cuda-test",
  srcs = ["cuda-test.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:profile",
    "//sling/myelin:builder",
    "//sling/myelin/cuda:cuda-runtime",
    "//sling/myelin/kernel:cuda",
    "//sling/myelin/kernel:tensorflow",
  ],
)

