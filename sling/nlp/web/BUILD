package(default_visibility = ["//visibility:public"])

cc_library(
  name = "text-extractor",
  srcs = ["text-extractor.cc"],
  hdrs = ["text-extractor.h"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/stream:input",
    "//sling/string:ctype",
    "//sling/string:strcat",
    "//sling/util:fingerprint",
    "//sling/web:html-parser",
  ],
)

cc_binary(
  name = "extract-text",
  srcs = ["extract-text.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:posix",
    "//sling/nlp/web:text-extractor",
    "//sling/stream:file-input",
    "//sling/string:strip",
    "//sling/web:rfc822-headers",
    "//sling/web:web-archive",
  ],
)

