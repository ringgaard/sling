package(default_visibility = ["//visibility:public"])

cc_library(
  name = "http-server",
  srcs = ["http-server.cc"],
  hdrs = ["http-server.h"],
  deps = [
    "//base",
    "//file",
    "//string:ctype",
    "//string:numbers",
    "//util:mutex",
    "//util:thread",
  ],
)

cc_library(
  name = "http-stream",
  srcs = ["http-stream.cc"],
  hdrs = ["http-stream.h"],
  deps = [
    ":http-server",
    "//stream",
  ],
)

cc_library(
  name = "static-content",
  srcs = ["static-content.cc"],
  hdrs = ["static-content.h"],
  deps = [
    ":http-server",
    "//base",
    "//file",
  ],
)

cc_library(
  name = "web-service",
  srcs = ["web-service.cc"],
  hdrs = ["web-service.h"],
  deps = [
    ":http-server",
    ":http-stream",
    "//base",
    "//frame:decoder",
    "//frame:encoder",
    "//frame:json",
    "//frame:printer",
    "//frame:object",
    "//frame:reader",
    "//frame:store",
    "//string:text",
  ],
)

