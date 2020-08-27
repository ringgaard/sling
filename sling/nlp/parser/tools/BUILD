package(default_visibility = ["//visibility:public"])

cc_binary(
  name = "parser-server",
  srcs = ["parser-server.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/net:http-server",
    "//sling/net:web-service",
    "//sling/nlp/document:document-tokenizer",
    "//sling/nlp/parser",
    "//sling/nlp/parser:components",
  ],
)

