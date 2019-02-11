package(default_visibility = ["//visibility:public"])

cc_library(
  name = "chart",
  srcs = ["chart.cc"],
  hdrs = ["chart.h"],
  deps = [
    "//sling/frame:store",
    "//sling/nlp/document",
    "//sling/nlp/kb:phrase-table",
  ],
)

cc_library(
  name = "resolver",
  srcs = ["resolver.cc"],
  hdrs = ["resolver.h"],
  deps = [
    "//sling/base",
    "//sling/frame:store",
    "//sling/frame:object",
    "//sling/nlp/kb:phrase-table",
    "//sling/util:top",
    "//sling/util:unicode",
  ],
)

cc_library(
  name = "measures",
  srcs = ["measures.cc"],
  hdrs = ["measures.h"],
  deps = [
    ":chart",
    "//sling/frame:store",
    "//sling/nlp/document",
    "//sling/nlp/kb:calendar",
    "//sling/nlp/kb:facts",
  ],
)

cc_binary(
  name = "parse-chart",
  srcs = ["parse-chart.cc"],
  deps = [
    ":chart",
    ":measures",
    "//sling/base",
    "//sling/file:posix",
    "//sling/file:recordio",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/nlp/document",
    "//sling/nlp/document:document-tokenizer",
    "//sling/nlp/document:lex",
    "//sling/nlp/kb:phrase-table",
  ],
)

