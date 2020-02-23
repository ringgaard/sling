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
    "//sling/util:fingerprint",
  ],
)

