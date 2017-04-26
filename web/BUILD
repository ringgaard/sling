package(default_visibility = ["//visibility:public"])

cc_library(
  name = "url",
  srcs = ["url.cc"],
  hdrs = ["url.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "rfc822-headers",
  srcs = ["rfc822-headers.cc"],
  hdrs = ["rfc822-headers.h"],
  deps = [
    "//base",
    "//stream:input",
    "//string:strip",
    "//string:text",
  ],
)

cc_library(
  name = "web-archive",
  srcs = ["web-archive.cc"],
  hdrs = ["web-archive.h"],
  deps = [
    ":rfc822-headers",
    "//base",
    "//stream",
    "//stream:bounded",
    "//stream:file-input",
    "//stream:input",
    "//string:numbers",
    "//string:text",
  ],
)

cc_library(
  name = "warc-reader",
  srcs = ["warc-reader.cc"],
  deps = [
    ":web-archive",
    "//stream:file-input",
    "//task",
    "//task:process",
  ],
  alwayslink = 1,
)

cc_library(
  name = "xml-parser",
  srcs = ["xml-parser.cc"],
  hdrs = ["xml-parser.h"],
  deps = [
    ":entity-ref",
    "//base",
    "//stream:input",
    "//string:ctype",
    "//util:unicode",
  ],
)

cc_library(
  name = "html-parser",
  srcs = ["html-parser.cc"],
  hdrs = ["html-parser.h"],
  deps = [
    ":entity-ref",
    ":xml-parser",
    "//base",
  ],
)

cc_library(
  name = "entity-ref",
  srcs = ["entity-ref.cc"],
  hdrs = ["entity-ref.h"],
  deps = [
    "//base",
  ],
)

