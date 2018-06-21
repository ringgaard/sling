package(default_visibility = ["//visibility:public"])

cc_library(
  name = "embedding-model",
  srcs = ["embedding-model.cc"],
  hdrs = ["embedding-model.h"],
  deps = [
    "//sling/myelin:builder",
    "//sling/myelin:flow",
  ],
)

cc_library(
  name = "word-embeddings",
  srcs = ["word-embeddings.cc"],
  deps = [
    ":embedding-model",
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

cc_library(
  name = "fact-embeddings",
  srcs = ["fact-embeddings.cc"],
  deps = [
    ":embedding-model",
    "//sling/base",
    "//sling/base:perf",
    "//sling/file",
    "//sling/file:textmap",
    "//sling/myelin:builder",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/myelin/kernel:tensorflow",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/frame:serialization",
    "//sling/nlp/kb:facts",
    "//sling/task:frames",
    "//sling/task:process",
    "//sling/util:embeddings",
    "//sling/util:bloom",
    "//sling/util:random",
    "//sling/util:sortmap",
    "//sling/util:thread",
  ],
  alwayslink = 1,
)
