package(default_visibility = ["//visibility:public"])

cc_library(
  name = "search-config",
  srcs = ["search-config.cc"],
  hdrs = ["search-config.h"],
  deps = [
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/frame:store",
    "//sling/nlp/document:phrase-tokenizer",
    "//sling/nlp/wiki:wiki",
    "//sling/util:unicode",
  ],
)

cc_library(
  name = "search-dictionary-builder",
  srcs = ["search-dictionary-builder.cc"],
  deps = [
    ":search-config",
    "//sling/base",
    "//sling/file:repository",
    "//sling/nlp/wiki:wiki",
    "//sling/task",
    "//sling/task:accumulator",
    "//sling/task:frames",
    "//sling/string:text",
    "//sling/util:arena",
    "//sling/util:mutex",
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
    ":search-config",
    ":search-dictionary",
    "//sling/base",
    "//sling/file:repository",
    "//sling/nlp/document:lex",
    "//sling/nlp/kb:calendar",
    "//sling/task",
    "//sling/task:frames",
    "//sling/string:text",
    "//sling/util:arena",
    "//sling/util:mutex",
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
    "//sling/string:text",
  ],
)

cc_library(
  name = "search-engine",
  srcs = ["search-engine.cc"],
  hdrs = ["search-engine.h"],
  deps = [
    ":search-index",
    "//sling/base",
    "//sling/nlp/document:phrase-tokenizer",
    "//sling/util:unicode",
    "//sling/util:top",
  ],
)

