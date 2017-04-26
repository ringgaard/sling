package(default_visibility = ["//visibility:public"])

cc_library(
  name = "text-extractor",
  srcs = ["text-extractor.cc"],
  hdrs = ["text-extractor.h"],
  deps = [
    "//base",
    "//file",
    "//stream:input",
    "//string:ctype",
    "//string:strcat",
    "//util:fingerprint",
    "//web:html-parser",
  ],
)

cc_binary(
  name = "extract-text",
  srcs = ["extract-text.cc"],
  deps = [
    "//base",
    "//file",
    "//file:posix",
    "//nlp/web:text-extractor",
    "//stream:file-input",
    "//string:strip",
    "//web:rfc822-headers",
    "//web:web-archive",
  ],
)

