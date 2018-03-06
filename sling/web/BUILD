package(default_visibility = ["//visibility:public"])

cc_library(
  name = "url",
  srcs = ["url.cc"],
  hdrs = ["url.h"],
  deps = [
    "//sling/base",
  ],
)

cc_library(
  name = "rfc822-headers",
  srcs = ["rfc822-headers.cc"],
  hdrs = ["rfc822-headers.h"],
  deps = [
    "//sling/base",
    "//sling/stream:input",
    "//sling/string:strip",
    "//sling/string:text",
  ],
)

cc_library(
  name = "web-archive",
  srcs = ["web-archive.cc"],
  hdrs = ["web-archive.h"],
  deps = [
    ":rfc822-headers",
    "//sling/base",
    "//sling/stream",
    "//sling/stream:bounded",
    "//sling/stream:file-input",
    "//sling/stream:input",
    "//sling/string:numbers",
    "//sling/string:text",
  ],
)

cc_library(
  name = "warc-reader",
  srcs = ["warc-reader.cc"],
  deps = [
    ":web-archive",
    "//sling/stream:file-input",
    "//sling/task",
    "//sling/task:process",
  ],
  alwayslink = 1,
)

cc_library(
  name = "xml-parser",
  srcs = ["xml-parser.cc"],
  hdrs = ["xml-parser.h"],
  deps = [
    ":entity-ref",
    "//sling/base",
    "//sling/stream:input",
    "//sling/string:ctype",
    "//sling/util:unicode",
  ],
)

cc_library(
<<<<<<< HEAD
  name = "html-parser",
  srcs = ["html-parser.cc"],
  hdrs = ["html-parser.h"],
  deps = [
    ":entity-ref",
    ":xml-parser",
    "//sling/base",
  ],
)

cc_library(
=======
>>>>>>> upstream/caspar
  name = "entity-ref",
  srcs = ["entity-ref.cc"],
  hdrs = ["entity-ref.h"],
  deps = [
    "//sling/base",
  ],
)

