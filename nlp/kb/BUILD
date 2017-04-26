package(default_visibility = ["//visibility:public"])

load("//tools:embed.bzl", "embed_data")

cc_library(
  name = "calendar",
  srcs = ["calendar.cc"],
  hdrs = ["calendar.h"],
  deps = [
    "//frame:object",
    "//frame:store",
    "//string:text",
    "//string:strcat",
  ],
)

embed_data(
  name = "app",
  srcs = [
    "app/appicon.ico",
    "app/index.html",
    "app/kb.css",
    "app/kb.js",
  ],
)

cc_library(
  name = "knowledge-service",
  srcs = ["knowledge-service.cc"],
  hdrs = ["knowledge-service.h"],
  deps = [
    ":calendar",
    ":app",
    "//frame:object",
    "//frame:serialization",
    "//frame:store",
    "//http:http-server",
    "//http:static-content",
    "//http:web-service",
    "//nlp/wiki:name-table",
  ],
)

cc_binary(
  name = "knowledge-server",
  srcs = ["knowledge-server.cc"],
  deps = [
    ":knowledge-service",
    "//base",
    "//file",
    "//file:embed",
    "//file:posix",
    "//http:http-server",
    "//http:web-service",
    "//string:text",
    "//string:strcat",
  ],
)

