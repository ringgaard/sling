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
    "//sling/myelin:compiler",
  ],
)

cc_library(
  name = "wavenet",
  srcs = ["wavenet.cc"],
  hdrs = ["wavenet.h"],
  deps = [
    "//sling/base",
    "//sling/myelin:compute",
    "//sling/myelin/generator:index",
    "//sling/myelin/generator:expression",
  ],
)

cc_binary(
  name = "wavenet-test",
  srcs = ["wavenet-test.cc"],
  deps = [
    ":wavenet",
    "//sling/base",
    "//sling/base:clock",
    "//sling/file:posix",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin:graph",
    "//sling/myelin:profile",
    "//sling/myelin/kernel:library",
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
    "//sling/myelin:compiler",
    "//sling/myelin:gradient",
    "//sling/myelin:learning",
    "//sling/myelin:profile",
    "//sling/myelin:rnn",
    "//sling/nlp/document",
    "//sling/nlp/document:lexical-features",
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
    "//sling/myelin:compiler",
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
    "//sling/myelin/kernel:library",
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
    "//sling/myelin/kernel:library",
  ],
)

cc_binary(
  name = "crf-test",
  srcs = ["crf-test.cc"],
  deps = [
    "//sling/base",
    "//sling/myelin:compiler",
    "//sling/myelin:crf",
    "//sling/myelin:flow",
    "//sling/myelin:builder",
  ],
)

