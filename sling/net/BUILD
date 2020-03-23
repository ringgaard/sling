package(default_visibility = ["//visibility:public"])

cc_library(
  name = "socket-server",
  srcs = ["socket-server.cc"],
  hdrs = ["socket-server.h"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)

cc_library(
  name = "http-protocol",
  srcs = ["http-protocol.cc"],
  hdrs = ["http-protocol.h"],
  deps = [
    ":socket-server",
    "//sling/base",
    "//sling/http:http-utils",
    "//sling/string:numbers",
    "//sling/util:mutex",
  ],
)

