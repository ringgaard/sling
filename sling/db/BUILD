package(default_visibility = ["//visibility:public"])

cc_library(
  name = "dbprotocol",
  hdrs = ["dbprotocol.h"],
  deps = [
    "//sling/base",
  ],
)

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
    ":dbprotocol",
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
    ":dbprotocol",
    "//sling/base",
    "//sling/file:posix",
    "//sling/net:http-server",
    "//sling/string:numbers",
    "//sling/util:fingerprint",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)

cc_library(
  name = "dbclient",
  srcs = ["dbclient.cc"],
  hdrs = ["dbclient.h"],
  deps = [
    ":dbprotocol",
    "//sling/base",
    "//sling/util:iobuffer",
  ],
)

