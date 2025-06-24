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
    "//sling/nlp/wiki:wiki",
    "//sling/task",
    "//sling/task:frames",
    "//sling/string:text",
    "//sling/util:json",
    "//sling/util:iobuffer",
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
    "//sling/util:json",
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

cc_library(
  name = "search-protocol",
  hdrs = ["search-protocol.h"],
  deps = [
    "//sling/base",
  ],
)

cc_binary(
  name = "search-server",
  srcs = ["search-server.cc"],
  deps = [
    ":search-engine",
    ":search-protocol",
    ":plain-snippet",
    "//sling/base",
    "//sling/base:clock",
    "//sling/file:posix",
    "//sling/file:recordio",
    "//sling/net:http-server",
    "//sling/util:json",
    "//sling/util:mutex",
  ],
)

cc_library(
  name = "search-client",
  srcs = ["search-client.cc"],
  hdrs = ["search-client.h"],
  deps = [
    ":search-protocol",
    "//sling/base",
    "//sling/frame:store",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/net:client",
    "//sling/stream:memory",
    "//sling/string:text",
    "//sling/util:mutex",
    "//sling/util:json",
  ],
)

cc_library(
  name = "plain-snippet",
  srcs = ["plain-snippet.cc"],
  deps = [
    ":search-engine",
    "//sling/base",
    "//sling/frame:store",
    "//sling/frame:object",
    "//sling/frame:serialization",
  ],
  alwayslink = 1,
)
