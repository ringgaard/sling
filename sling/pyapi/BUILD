package(default_visibility = ["//visibility:public"])

cc_library(
  name = "hello",
  srcs = ["hello.cc"],
  deps = [
    "//sling/base",
  ],
)

cc_library(
  name = "frames",
  srcs = ["frames.cc"],
  hdrs = ["frames.h"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/frame",
    "//sling/stream:file",
    "//sling/stream:unix-file",
  ],
  copts = [
    "-Wno-pmf-conversions",
  ]
)

cc_binary(
  name = "pysling.so",
  srcs = ["api.cc"],
  deps = [
    ":frames",
    ":hello",
    "//sling/base",
    "//sling/base:libinit",
    "//sling/file:posix",
  ],
  linkshared = 1,
  linkstatic = 1,
)

