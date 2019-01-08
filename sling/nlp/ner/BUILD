package(default_visibility = ["//visibility:public"])

cc_library(
  name = "chart",
  srcs = ["chart.cc"],
  hdrs = ["chart.h"],
  deps = [
    "//sling/frame:store",
    "//sling/nlp/document",
    "//sling/nlp/document:fingerprinter",
    "//sling/nlp/kb:phrase-table",
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
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/nlp/document",
    "//sling/nlp/document:document-tokenizer",
    "//sling/nlp/document:lex",
    "//sling/nlp/kb:phrase-table",
  ],
)

