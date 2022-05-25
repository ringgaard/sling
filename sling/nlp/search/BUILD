package(default_visibility = ["//visibility:public"])

cc_library(
  name = "item-terms",
  srcs = ["item-terms.cc"],
  hdrs = ["item-terms.h"],
  deps = [
    "//sling/base",
    "//sling/file:repository",
    "//sling/nlp/document:phrase-tokenizer",
    "//sling/task",
    "//sling/task:frames",
    "//sling/string:text",
    "//sling/util:arena",
    "//sling/util:mutex",
    "//sling/util:unicode",
  ],
  alwayslink = 1,
)

