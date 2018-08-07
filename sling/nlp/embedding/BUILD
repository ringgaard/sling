package(default_visibility = ["//visibility:public"])

cc_library(
  name = "embedding-model",
  srcs = ["embedding-model.cc"],
  hdrs = ["embedding-model.h"],
  deps = [
    "//sling/myelin:builder",
    "//sling/myelin:flow",
    "//sling/myelin:gradient",
    "//sling/util:random",
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
    "//sling/myelin:compiler",
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
    "//sling/file:textmap",
    "//sling/myelin:builder",
    "//sling/myelin:compiler",
    "//sling/myelin:learning",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/frame:serialization",
    "//sling/nlp/kb:facts",
    "//sling/task:frames",
    "//sling/task:process",
    "//sling/util:bloom",
    "//sling/util:embeddings",
    "//sling/util:mutex",
    "//sling/util:random",
    "//sling/util:sortmap",
    "//sling/util:thread",
  ],
  alwayslink = 1,
)

cc_binary(
  name = "category-facts",
  srcs = ["category-facts.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/frame:serialization",
    "//sling/myelin:builder",
    "//sling/myelin:compiler",
    "//sling/util:embeddings",
    "//sling/util:top",
  ],
)

