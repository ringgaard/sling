package(default_visibility = ["//visibility:public"])

cc_library(
  name = "search-dictionary-builder",
  srcs = ["search-dictionary-builder.cc"],
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

cc_library(
  name = "search-dictionary",
  srcs = ["search-dictionary.cc"],
  hdrs = ["search-dictionary.h"],
  deps = [
    "//sling/base",
    "//sling/file:repository",
    "//sling/string:text",
    "//sling/util:fingerprint",
  ],
)

cc_library(
  name = "search-index-builder",
  srcs = ["search-index-builder.cc"],
  deps = [
    ":search-dictionary",
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

cc_library(
  name = "search-index",
  srcs = ["search-index.cc"],
  hdrs = ["search-index.h"],
  deps = [
    "//sling/base",
    "//sling/file:repository",
  ],
)

cc_library(
  name = "search-engine",
  srcs = ["search-engine.cc"],
  hdrs = ["search-engine.h"],
  deps = [
    "//sling/base",
  ],
)

