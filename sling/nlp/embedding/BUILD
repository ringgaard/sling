package(default_visibility = ["//visibility:public"])

cc_library(
  name = "word-embeddings",
  srcs = ["word-embeddings.cc"],
  deps = [
    "//sling/base",
    "//sling/base:perf",
    "//sling/file:recordio",
    "//sling/file:textmap",
    "//sling/myelin:builder",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin/kernel:tensorflow",
    "//sling/nlp/document",
    "//sling/frame:serialization",
    "//sling/task:accumulator",
    "//sling/task:documents",
    "//sling/task:process",
    "//sling/util:embeddings",
    "//sling/util:random",
    "//sling/util:thread",
    "//sling/util:unicode",
  ],
  alwayslink = 1,
)

