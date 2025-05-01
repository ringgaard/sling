package(default_visibility = ["//visibility:public"])

load("//tools:embed.bzl", "embed_data")

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

embed_data(
  name = "app",
  srcs = [
    "app/index.html",
    "app/dbadmin.js",
  ],
)

cc_library(
  name = "dbserver",
  srcs = ["dbserver.cc"],
  hdrs = ["dbserver.h"],
  deps = [
    ":app",
    ":db",
    ":dbprotocol",
    "//app",
    "//sling/base",
    "//sling/file:embed",
    "//sling/net:http-server",
    "//sling/net:static-content",
    "//sling/util:fingerprint",
    "//sling/util:json",
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
    "//sling/net:client",
    "//sling/util:iobuffer",
  ],
)

cc_binary(
  name = "slingdb",
  srcs = ["slingdb.cc"],
  deps = [
    ":dbserver",
    "//sling/base",
    "//sling/file:posix",
    "//sling/net:http-server",
  ],
)

cc_binary(
  name = "check-index",
  srcs = ["check-index.cc"],
  deps = [
    ":dbserver",
    "//sling/base",
    "//sling/file:posix",
  ],
)
