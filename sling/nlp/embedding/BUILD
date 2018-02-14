package(default_visibility = ["//visibility:public"])

cc_library(
  name = "vocabulary",
  srcs = ["vocabulary.cc"],
  deps = [
    "//sling/base",
    "//sling/file:recordio",
    "//sling/file:textmap",
    "//sling/nlp/document",
    "//sling/frame:serialization",
    "//sling/task:accumulator",
    "//sling/task:documents",
    "//sling/task:process",
    "//sling/util:thread",
    "//sling/util:unicode",
  ],
  alwayslink = 1,
)

