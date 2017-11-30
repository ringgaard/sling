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
    "//sling/frame:store",
  ],
)

cc_binary(
  name = "sling.so",
  srcs = ["api.cc"],
  deps = [
    ":frames",
    ":hello",
    "//sling/base:libinit",
    "//sling/file:posix",
  ],
  linkshared = 1,
  linkstatic = 1,
)

