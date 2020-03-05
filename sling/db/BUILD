package(default_visibility = ["//visibility:public"])

cc_library(
  name = "dbindex",
  srcs = ["dbindex.cc"],
  hdrs = ["dbindex.h"],
  deps = [
    "//sling/base",
    "//sling/file",
  ],
)

cc_library(
  name = "db",
  srcs = ["db.cc"],
  hdrs = ["db.h"],
  deps = [
    ":dbindex",
    "//sling/base",
    "//sling/file",
    "//sling/file:recordio",
    "//sling/string:numbers",
    "//sling/string:text",
    "//sling/util:fingerprint",
  ],
)

cc_binary(
  name = "dbserver",
  srcs = ["dbserver.cc"],
  deps = [
    ":db",
    "//sling/base",
    "//sling/file:posix",
    "//sling/http:http-server",
    "//sling/string:numbers",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)

