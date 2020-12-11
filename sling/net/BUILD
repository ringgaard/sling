package(default_visibility = ["//visibility:public"])

cc_library(
  name = "socket-server",
  srcs = ["socket-server.cc"],
  hdrs = ["socket-server.h"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/util:iobuffer",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)

cc_library(
  name = "http-utils",
  srcs = ["http-utils.cc"],
  hdrs = ["http-utils.h"],
  deps = [
    "//sling/base",
    "//sling/string:ctype",
    "//sling/string:text",
    "//sling/string:numbers",
  ],
)

cc_library(
  name = "http-server",
  srcs = ["http-server.cc"],
  hdrs = ["http-server.h"],
  deps = [
    ":socket-server",
    ":http-utils",
    "//sling/base",
    "//sling/string:numbers",
    "//sling/util:iobuffer",
    "//sling/util:mutex",
  ],
)

cc_library(
  name = "static-content",
  srcs = ["static-content.cc"],
  hdrs = ["static-content.h"],
  deps = [
    ":http-server",
    ":http-utils",
    "//sling/base",
    "//sling/file",
  ],
)

cc_library(
  name = "media-service",
  srcs = ["media-service.cc"],
  hdrs = ["media-service.h"],
  deps = [
    ":http-server",
    ":http-utils",
    "//sling/base",
    "//sling/db:dbclient",
    "//sling/util:mutex",
  ],
)

cc_library(
  name = "proxy-service",
  srcs = ["proxy-service.cc"],
  hdrs = ["proxy-service.h"],
  deps = [
    ":http-server",
    "//sling/base",
  ],
  linkopts = [
    "-lcurl",
  ],
)

cc_library(
  name = "web-service",
  srcs = ["web-service.cc"],
  hdrs = ["web-service.h"],
  deps = [
    ":http-server",
    ":http-utils",
    "//sling/base",
    "//sling/frame:decoder",
    "//sling/frame:encoder",
    "//sling/frame:json",
    "//sling/frame:printer",
    "//sling/frame:object",
    "//sling/frame:reader",
    "//sling/frame:store",
    "//sling/stream:memory",
    "//sling/string:text",
  ],
)

